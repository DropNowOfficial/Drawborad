"""Drive the packaged native editor in Chromium on an isolated Compose host."""
import base64
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from urllib.parse import urlsplit
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw
from bucket_artifact_evidence import verify_bucket_artifacts
from playwright.sync_api import expect, sync_playwright


def endpoint(method, suffix):
    return lambda r: r.request.method == method and urlsplit(r.url).path.endswith(suffix)


def authenticate(page, url, token, *, reconnect=False):
    with page.expect_response(endpoint("GET", "/capabilities")) as probe:
        page.goto(url)
    assert probe.value.status == 401
    field = page.get_by_label("Access token", exact=True)
    expect(field).to_have_value("")
    if reconnect:
        expect(page.locator("#nativeError")).to_contain_text("Bearer token required")
    expect(page.locator("#nativeNew")).to_be_enabled()
    with page.expect_response(endpoint("GET", "/capabilities")) as authenticated:
        field.fill(token)
        field.press("Tab")
    assert authenticated.value.status == 200
    assert authenticated.value.json()["native_engine_configured"] is True
    if reconnect:
        expect(page.locator("#nativeSave")).to_be_enabled()


def click_post(page, control, suffix, status=200):
    with page.expect_response(endpoint("POST", suffix)) as request:
        control.click()
    response = request.value
    assert response.status == status, f"{suffix} returned {response.status}: {response.text()}"
    expect(page.locator("#nativeNew")).to_be_enabled(timeout=90000)
    return response.json()


def preview(page, destination=None):
    image = page.get_by_alt_text("Native painting preview")
    image.evaluate("(image) => image.decode()")
    # Compare the actual displayed image at its native dimensions, not a CSS
    # screenshot resampled at a different subpixel scroll/layout position.
    # Real screenshots remain separate visual evidence.
    if destination:
        image.screenshot(path=destination, animations="disabled")
    encoded = image.evaluate("""image => {
        const canvas=document.createElement('canvas');
        canvas.width=image.naturalWidth;canvas.height=image.naturalHeight;
        canvas.getContext('2d').drawImage(image,0,0);
        return canvas.toDataURL('image/png').split(',')[1];
    }""")
    return Image.open(io.BytesIO(base64.b64decode(encoded))).convert("RGBA").tobytes()


def download(page, button, output, filename):
    with page.expect_download() as transfer:
        page.get_by_role("button", name=button, exact=True).click()
    assert transfer.value.suggested_filename == filename
    target = output / filename
    transfer.value.save_as(target)
    assert target.stat().st_size > 0
    return target


def verify_layer_selection(page, state):
    """Selection scopes native stroke IDs without changing visibility or revision."""
    select = page.locator("#nativeStrokes")
    before = preview(page)
    expected_status = page.locator("#nativeStatus").inner_text()
    layers = [n for n in state["nodes"] if n["kind"] == 3]
    empty = 0
    page.locator("#nativeShowSublayers").check()
    for node in state["nodes"]:
        if node["kind"] not in (1, 3):
            continue
        expected = [e["id"] for e in state["strokes"]
                    if e["layer_id"] == node["id"] or e["object_id"] == node["id"]]
        button = page.locator('[data-node-id="' + node["id"] + '"] .native-node-select')
        button.click()
        expect(button).to_have_attribute("aria-pressed", "true")
        expect(page.locator("#nativeScope")).to_contain_text(node["name"])
        assert select.locator("option").evaluate_all("options => options.map(o => o.value)") == expected
        expect(page.locator("#nativeStatus")).to_have_text(expected_status)
        if not expected:
            empty += node["kind"] == 3
            expect(select).to_be_disabled()
            expect(page.locator("#nativeRecolor")).to_be_disabled()
    expect(page.locator("#nativeLayerSummary")).to_contain_text(str(len(layers)) + " paint layers")
    expect(page.locator("#nativeLayerSummary")).to_contain_text(str(empty) + " without strokes")
    page.locator("#nativeShowSublayers").uncheck()
    page.locator("#nativeAllStrokes").click()
    assert select.locator("option").evaluate_all("options => options.map(o => o.value)") == [e["id"] for e in state["strokes"]]
    assert preview(page) == before


def verify_controls_during_preview(route):
    """The response has rebuilt rows, but the native command is still busy."""
    page = route.request.frame.page
    controls = page.locator("#nativeNodes input, #nativeNodes button")
    assert controls.count() > 0
    assert controls.evaluate_all("controls => controls.every(c => c.disabled)")
    expect(page.locator("#nativeAllStrokes")).to_be_disabled()
    expect(page.locator("#nativeShowSublayers")).to_be_disabled()
    route.continue_()



def exercise_bucket(page, initial, original_draw, original_ora, output, url, token):
    """Real Chromium/engine checkpoint. Run only as part of the isolated CI host."""
    output.mkdir(parents=True, exist_ok=True)
    original_bytes = original_draw.read_bytes()
    with zipfile.ZipFile(original_ora) as archive:
        original_layers = len(ET.fromstring(archive.read("stack.xml")).findall(".//layer"))
    before = preview(page, output / "before.png")
    added = click_post(page, page.locator("#nativeAddFillLayer"), "/commands")
    old_nodes = {node["id"] for node in initial["nodes"]}
    new_layers = [node for node in added["nodes"] if node["kind"] == 3 and node["id"] not in old_nodes]
    assert len(new_layers) == 1
    layer = new_layers[0]
    assert layer["name"] == "Color fill" and layer["visible"] and not layer["locked"]
    expect(page.locator("#nativeShowSublayers")).to_be_checked()
    expect(page.locator('[data-node-id="' + layer["id"] + '"] .native-node-select')).to_have_attribute("aria-pressed", "true")
    assert preview(page) == before, "Adding an empty layer must not change artwork"
    # Chromium's native OS color picker is outside this browser acceptance.
    page.locator("#nativeColor").evaluate("""input => {
        input.value = '#c020b0';
        input.dispatchEvent(new Event('input', {bubbles:true}));
        input.dispatchEvent(new Event('change', {bubbles:true}));
    }""")
    page.locator("#nativeTolerance").fill("255")
    page.locator("#nativeBucket").check()
    filled = click_post(page, page.locator("#nativePreview"), "/commands")
    assert int(filled["stroke_count"]) == int(added["stroke_count"]) + 1
    assert filled["strokes"][-1]["tool"] == "bucket"
    assert filled["strokes"][-1]["layer_id"] == layer["id"]
    after = preview(page, output / "filled.png")
    assert after != before
    click_post(page, page.locator("#nativeUndo"), "/undo")
    assert preview(page) == before
    redone = click_post(page, page.locator("#nativeRedo"), "/redo")
    assert preview(page) == after
    rejected = click_post(page, page.locator("#nativeSave"), "/save", 422)
    assert rejected["detail"]["code"] == "unsupported"
    assert rejected["detail"]["message"] == "save.format-copy-required"
    expect(page.locator("#nativeDownload")).to_be_disabled()
    assert preview(page) == after
    copied = click_post(page, page.locator("#nativeSaveCopy"), "/save-copy", 201)
    assert copied["id"] != initial["id"] and not copied["dirty"]
    assert copied["revision"] == redone["revision"]
    assert copied["strokes"] == redone["strokes"]
    authenticate(page, url, token, reconnect=True)
    expect(page.locator("#nativeStatus")).to_contain_text("Saved · revision " + copied["revision"])
    assert preview(page) == after
    page.screenshot(path=output / "saved-editor.png", mask=[page.locator("#token")])
    draw = download(page, "Download saved .draw", output, "painting.draw")
    png = download(page, "Export painting PNG", output, "painting.png")
    ora = download(page, "Export painted layers ORA", output, "painting.ora")
    evidence = verify_bucket_artifacts(ora, png, original_layers + 1,
                                      (int(copied["width"]), int(copied["height"])))
    # Independent authenticated disk-read check: save-copy must leave the old
    # project's bytes intact. Restore the current server selection afterwards.
    auth = {"Authorization": "Bearer " + token}
    old = page.request.get(url.rstrip("/") + "/api/v1/projects/" + initial["id"] + "/download", headers=auth)
    assert old.status == 200 and old.body() == original_bytes
    restored = page.request.get(url.rstrip("/") + "/api/v1/projects/" + copied["id"], headers=auth)
    assert restored.status == 200 and restored.json()["revision"] == copied["revision"]
    click_post(page, page.locator("#nativeClose"), "/close")
    page.locator("#nativeFile").set_input_files(draw)
    reopened = click_post(page, page.locator("#nativeImport"), "/import", 201)
    assert reopened["strokes"] == copied["strokes"]
    assert preview(page) == after, "Downloaded .draw must reopen to the same visible fill"
    receipt = {"browser_workflow": "passed", "project": copied["id"], "layer": layer["id"],
               "color_input": "DOM value + input/change; native OS picker not tested",
               "stroke_count": copied["stroke_count"], "saved_revision": copied["revision"],
               "legacy_save_rejected": 422, "legacy_file_unchanged": True,
               "undo_redo_pixels_equal": True, "download_reopen_pixels_equal": True,
               "exports": [draw.name, png.name, ora.name], "artifact_evidence": evidence}
    (output / "bucket-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def exercise_lineart(page, output, url, token):
    """Technical closed-region fixture, not user artwork or a quality claim."""
    output.mkdir(parents=True, exist_ok=True)
    original = Image.new("RGBA", (512, 512), "white")
    pen = ImageDraw.Draw(original)
    for x0, x1 in ((40, 230), (280, 470)):
        pen.rectangle((x0, 40, x1, 470), outline=(128, 128, 128, 255), width=6)
        pen.rectangle((x0 + 2, 42, x1 - 2, 468), outline=(0, 0, 0, 255), width=2)
    source = output / "technical-two-closed-regions.png"
    original.save(source, dpi=(300, 300))
    original_bytes = source.read_bytes()
    click_post(page, page.locator("#nativeClose"), "/close")
    page.locator("#nativeFile").set_input_files(source)
    initial = click_post(page, page.locator("#nativeImport"), "/import-lineart", 201)
    assert (int(initial["width"]), int(initial["height"])) == (512, 512)
    lineart = [n for n in initial["nodes"] if n["stage"] == "drawborad.lineart.rgba.v1"]
    assert len(lineart) == 1
    lineart = lineart[0]
    assert lineart["locked"] and lineart["visible"] and lineart["blend"] == 1
    assert lineart["source_kind"] == 0
    assert "ora" in initial["capabilities"]
    expect(page.locator("#nativeOra")).to_be_enabled()

    def panel(name):
        page.locator("#nativeEditor").scroll_into_view_if_needed()
        page.locator("#nativeEditor").screenshot(path=output / (name + "-panel.png"),
            mask=[page.locator("#token")], animations="disabled")

    def exported(name):
        path = download(page, "Export painting PNG", output, "painting.png")
        with Image.open(path) as image:
            pixels = image.convert("RGBA")
            assert pixels.size == (512, 512)
        (output / (name + "-native.png")).write_bytes(path.read_bytes())
        return pixels

    before = preview(page, output / "before-preview.png")
    assert exported("before").tobytes() == original.tobytes()
    old_nodes = {n["id"] for n in initial["nodes"]}
    added = click_post(page, page.locator("#nativeAddFillLayer"), "/commands")
    layers = [n for n in added["nodes"] if n["kind"] == 3 and n["id"] not in old_nodes]
    assert len(layers) == 1
    color = layers[0]
    assert color["name"] == "Color fill" and color["visible"] and not color["locked"]
    parent = next(n for n in added["nodes"] if n["id"] == lineart["parent_id"])
    assert color["parent_id"] == parent["id"]
    assert parent["children"].index(color["id"]) < parent["children"].index(lineart["id"])
    assert preview(page) == before
    expect(page.locator('[data-node-id="' + color["id"] + '"] .native-node-select')).to_have_attribute("aria-pressed", "true")
    panel("before")
    page.locator("#nativeColor").evaluate("""input => {
        input.value = '#00ffff';
        input.dispatchEvent(new Event('input', {bubbles:true}));
        input.dispatchEvent(new Event('change', {bubbles:true}));
    }""")
    page.locator("#nativeTolerance").fill("0")
    page.locator("#nativeBucket").check()
    image = page.locator("#nativePreview")
    image.scroll_into_view_if_needed()
    bounds = image.bounding_box()
    assert bounds is not None
    with page.expect_response(endpoint("POST", "/commands")) as pending:
        page.mouse.click(bounds["x"] + bounds["width"] * 100 / 512,
                         bounds["y"] + bounds["height"] * 200 / 512)
    response = pending.value
    assert response.status == 200, f"lineart bucket returned {response.status}: {response.text()}"
    filled = response.json()
    expect(page.locator("#nativeNew")).to_be_enabled(timeout=90000)
    assert filled["strokes"][-1]["tool"] == "bucket"
    assert filled["strokes"][-1]["layer_id"] == color["id"]
    assert int(filled["stroke_count"]) == int(added["stroke_count"]) + 1
    after = preview(page, output / "after-preview.png")
    panel("after")
    painted = exported("after")
    expected = original.copy()
    ImageDraw.Draw(expected).rectangle((46, 46, 224, 464), fill=(0, 255, 255, 255))
    assert painted.tobytes() == expected.tobytes(), "Only the left closed white region may change; black/grey boundaries, right region and exterior must remain exact"
    assert painted.getpixel((100, 200)) == (0, 255, 255, 255)
    assert painted.getpixel((350, 200)) == original.getpixel((350, 200))
    assert after != before
    click_post(page, page.locator("#nativeUndo"), "/undo")
    assert preview(page) == before
    assert exported("undo").tobytes() == original.tobytes()
    panel("undo")
    redone = click_post(page, page.locator("#nativeRedo"), "/redo")
    assert preview(page) == after
    copied = click_post(page, page.locator("#nativeSaveCopy"), "/save-copy", 201)
    assert copied["id"] != initial["id"] and not copied["dirty"]
    assert copied["revision"] == redone["revision"] and copied["strokes"] == redone["strokes"]
    draw = download(page, "Download saved .draw", output, "painting.draw")
    png = download(page, "Export painting PNG", output, "painting.png")
    with Image.open(png) as saved_png:
        assert saved_png.convert("RGBA").tobytes() == expected.tobytes()
    ora = download(page, "Export painted layers ORA", output, "painting.ora")
    with zipfile.ZipFile(ora) as archive:
        assert archive.testzip() is None
        layers = ET.fromstring(archive.read("stack.xml")).findall(".//layer")
        ink = next(n for n in layers if n.get("name") == "Original lineart")
        fill = next(n for n in layers if n.get("name") == "Color fill")
        assert ink.get("composite-op") == "svg:multiply" and layers.index(ink) < layers.index(fill)
        assert Image.open(io.BytesIO(archive.read(ink.get("src")))).convert("RGBA").tobytes() == original.tobytes()
        assert Image.open(io.BytesIO(archive.read("mergedimage.png"))).convert("RGBA").tobytes() == expected.tobytes()
    click_post(page, page.locator("#nativeClose"), "/close")
    page.locator("#nativeFile").set_input_files(draw)
    reopened = click_post(page, page.locator("#nativeImport"), "/import", 201)
    assert reopened["strokes"] == copied["strokes"]
    assert preview(page) == after
    assert exported("reopened").tobytes() == expected.tobytes()
    panel("reopened")
    assert source.read_bytes() == original_bytes
    receipt = {"browser_workflow": "passed", "fixture": "technical synthetic two closed regions, not user artwork",
        "size": [512, 512], "source_unchanged": True, "lineart_locked_multiply": True,
        "fill_layer_below_lineart": True, "seed": [100, 200], "tolerance": 0,
        "left_region_cyan_only": True, "right_exterior_black_grey_unchanged": True,
        "undo_redo_pixels_equal": True, "download_reopen_pixels_equal": True,
        "color_input": "DOM input/change; native OS picker not tested",
        "ora": "downloaded independent ink/fill layers, Multiply order and merged pixels verified",
        "project": copied["id"], "saved_revision": copied["revision"],
        "exports": [draw.name, png.name, ora.name],
        "screenshots": [name + "-panel.png" for name in ("before", "after", "undo", "reopened")]}
    (output / "lineart-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    # Actual new pen entry: a native subpixel stroke, not a painted browser overlay.
    page.locator("#nativeShowSublayers").check()
    page.locator('[data-node-id="' + color["id"] + '"] .native-node-select').click()
    page.locator("#nativePenWidth").fill("0.25")
    page.locator("#nativeColor").evaluate("input => {input.value='#000000';input.dispatchEvent(new Event('input',{bubbles:true}));}")
    page.locator("#nativePen").check()
    image.scroll_into_view_if_needed()
    bounds=image.bounding_box();scale=min(bounds["width"]/512,bounds["height"]/512)
    ox=bounds["x"]+(bounds["width"]-512*scale)/2;oy=bounds["y"]+(bounds["height"]-512*scale)/2
    with page.expect_response(endpoint("POST", "/commands")) as painted:
        page.mouse.move(ox+20*scale,oy+490.5*scale)
        page.mouse.down()
        page.mouse.move(ox+200*scale,oy+490.5*scale,steps=20)
        page.mouse.up()
    assert painted.value.status==200,painted.value.text()
    expect(page.locator("#nativeNew")).to_be_enabled(timeout=90000)
    penstate=painted.value.json();assert penstate["strokes"][-1]["tool"]=="fineliner-aa"
    pen_pixels=exported("fine-pen").tobytes();assert pen_pixels!=expected.tobytes()
    center=(490*512+100)*4
    assert pen_pixels[center:center+4]==bytes([191,191,191,255])
    panel("fine-pen")
    click_post(page,page.locator("#nativeUndo"),"/undo")
    assert exported("fine-pen-undo").tobytes()==expected.tobytes()
    click_post(page,page.locator("#nativeRedo"),"/redo")
    assert exported("fine-pen-redo").tobytes()==pen_pixels
    pen_saved=click_post(page,page.locator("#nativeSaveCopy"),"/save-copy",201)
    pen_dir=output/"fine-pen-download";pen_dir.mkdir(exist_ok=True)
    pen_draw=download(page,"Download saved .draw",pen_dir,"painting.draw")
    pen_draw.rename(output/"fine-pen.draw")
    click_post(page,page.locator("#nativeClose"),"/close")
    page.locator("#nativeFile").set_input_files(output/"fine-pen.draw")
    click_post(page,page.locator("#nativeImport"),"/import",201)
    assert exported("fine-pen-reopened").tobytes()==pen_pixels
    panel("fine-pen-reopened")
    (output/"fine-pen-receipt.json").write_text(json.dumps({"workflow":"passed","width":.25,"tool":"fineliner-aa","pointer_input":"real Chromium mouse","undo_redo_reopen_equal":True,"project":pen_saved["id"]},indent=2))
    page.locator("#nativeShowSublayers").check()
    page.locator('[data-node-id="' + color["id"] + '"] .native-node-select').click()
    page.locator("#nativeBrush").select_option("dry_ink")
    page.locator("#nativePenWidth").fill("12")
    page.locator("#nativePen").check()
    image.scroll_into_view_if_needed();bounds=image.bounding_box()
    scale=min(bounds["width"]/512,bounds["height"]/512);ox=bounds["x"]+(bounds["width"]-512*scale)/2;oy=bounds["y"]+(bounds["height"]-512*scale)/2
    with page.expect_response(endpoint("POST", "/commands")) as dry_response:
        page.mouse.move(ox+20*scale,oy+480*scale);page.mouse.down()
        page.mouse.move(ox+200*scale,oy+480*scale,steps=16);page.mouse.up()
    assert dry_response.value.status==200,dry_response.value.text()
    expect(page.locator("#nativeNew")).to_be_enabled(timeout=90000)
    assert dry_response.value.json()["strokes"][-1]["tool"]=="dry-ink"
    dry_pixels=exported("dry-ink").tobytes();assert dry_pixels!=pen_pixels
    panel("dry-ink")
    click_post(page,page.locator("#nativeUndo"),"/undo");assert exported("dry-ink-undo").tobytes()==pen_pixels
    click_post(page,page.locator("#nativeRedo"),"/redo");assert exported("dry-ink-redo").tobytes()==dry_pixels
    click_post(page,page.locator("#nativeSaveCopy"),"/save-copy",201)
    dry_dir=output/"dry-ink-download";dry_dir.mkdir(exist_ok=True)
    dry_draw=download(page,"Download saved .draw",dry_dir,"painting.draw")
    click_post(page,page.locator("#nativeClose"),"/close");page.locator("#nativeFile").set_input_files(dry_draw)
    click_post(page,page.locator("#nativeImport"),"/import",201);assert exported("dry-ink-reopened").tobytes()==dry_pixels
    (output/"dry-ink-receipt.json").write_text(json.dumps({"workflow":"passed","tool":"dry-ink","pointer_input":"real Chromium mouse","undo_redo_reopen_equal":True,"artistic_quality":"not assessed by this functional check"},indent=2))
    return receipt


def exercise_mypaint(page, output, width=512, height=512):
    """Actual optional-library UI strokes, not a finished-art acceptance."""
    output.mkdir(parents=True,exist_ok=True)
    click_post(page,page.locator('#nativeClose'),'/close')
    page.locator('#nativeWidth').fill(str(width));page.locator('#nativeHeight').fill(str(height))
    initial=click_post(page,page.locator('#nativeNew'),'/projects',201)
    assert 'mypaint_ink' in initial['capabilities'],initial.get('mypaint_status')
    page.locator('#nativeBrush').select_option('mypaint_ink')
    page.locator('#nativePen').check()
    expect(page.locator('#nativePenWidth')).to_be_disabled()
    expect(page.locator('#nativeColor')).to_be_disabled()
    before=preview(page,output/'before.png');state=initial
    for index,curve in enumerate([[(.20,.38),(.36,.31),(.57,.31),(.77,.39)],[(.20,.40),(.37,.51),(.58,.57),(.75,.57)]]):
        image=page.locator('#nativePreview');image.scroll_into_view_if_needed();box=image.bounding_box();assert box
        points=[]
        for i in range(31):
            t=i/30;u=1-t
            x=u**3*curve[0][0]+3*u*u*t*curve[1][0]+3*u*t*t*curve[2][0]+t**3*curve[3][0]
            y=u**3*curve[0][1]+3*u*u*t*curve[1][1]+3*u*t*t*curve[2][1]+t**3*curve[3][1]
            if width>512:
                x=(450+x*512)/width;y=(450+y*512)/height
            points.append((box['x']+x*box['width'],box['y']+y*box['height']))
        page.mouse.move(*points[0]);page.mouse.down()
        for point in points[1:]:page.mouse.move(*point)
        with page.expect_response(endpoint('POST','/commands')) as response:page.mouse.up()
        assert response.value.status==200,response.value.text();state=response.value.json()
        expect(page.locator('#nativeNew')).to_be_enabled(timeout=90000)
        assert len(state['nodes'])==len(initial['nodes'])+index+1
        assert state['strokes']==initial['strokes'],'raster strokes must not impersonate native events'
    after=preview(page,output/'after.png');assert after!=before
    click_post(page,page.locator('#nativeUndo'),'/undo');undone=preview(page,output/'undo.png');assert undone!=after
    click_post(page,page.locator('#nativeRedo'),'/redo');assert preview(page)==after
    saved=click_post(page,page.locator('#nativeSaveCopy'),'/save-copy',201)
    page.locator('#nativeEditor').screenshot(path=output/'actual-editor.png',mask=[page.locator('#token')])
    draw=download(page,'Download saved .draw',output,'painting.draw')
    png=download(page,'Export painting PNG',output,'painting.png')
    ora=download(page,'Export painted layers ORA',output,'painting.ora')
    assert b'mypaint-pointy-ink-raster-v1' in draw.read_bytes()
    click_post(page,page.locator('#nativeClose'),'/close');page.locator('#nativeFile').set_input_files(draw)
    reopened=click_post(page,page.locator('#nativeImport'),'/import',201)
    assert preview(page,output/'reopened.png')==after
    added=[n for n in reopened['nodes'] if n['name']=='MyPaint ink (experimental raster)']
    assert len(added)==2 and all(n['locked'] and n['blend']==1 for n in added)
    receipt={'actual_chromium_mouse':True,'real_mypaint_worker':True,'independent_locked_layers':2,'undo_redo':True,'download_reopen_pixel_equal':True,'manifest_in_draw':True,'canvas':[width,height],'scope':'actual native canvas with local technical eyelid strokes; not finished artwork'}
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt


def exercise(context, page, source, output, url, token, restart):
    authenticate(page, url, token)
    page.get_by_text("Photo segmentation & source-layer review", exact=True).click()
    page.locator("#image").set_input_files(source / "source.png")
    page.locator("#seedFile").set_input_files(source / "seeds.png")
    job = click_post(page, page.locator("#guidedRun"), "/guided", 201)
    assert job["backend"] == "opencv-grabcut" and not job["ai_inference"]
    expect(page.locator("#layerList img").first).to_be_visible()
    state = click_post(page, page.locator("#nativeFromJob"), "/from-job/" + job["id"], 201)
    verify_layer_selection(page, state)
    stroke = state["strokes"][-1]
    select = page.locator("#nativeStrokes")
    select.select_option(index=0)
    select.select_option(value=stroke["id"])
    expect(page.locator("#nativeStrokeInfo")).to_contain_text(stroke["id"])
    before = preview(page, output / "before.png")

    peer = context.new_page()
    authenticate(peer, url, token, reconnect=True)
    page.route("**/projects/*/preview.png", verify_controls_during_preview, times=1)
    changed = click_post(page, page.locator("#nativeRecolor"), "/commands")
    assert changed["dirty"] and changed["strokes"][-1]["color"] != stroke["color"]
    expect(page.locator("#nativeDownload")).to_be_disabled()
    after = preview(page, output / "after.png")
    assert before != after, "Recolor must change the rendered browser image"

    owner = next(n for n in state["nodes"] if n["id"] == stroke["object_id"])
    lock_label = owner["name"] + " locked"
    peer_lock = peer.get_by_role("checkbox", name=lock_label, exact=True)
    stale = click_post(peer, peer_lock, "/commands", 409)
    assert stale["detail"]["code"] == "stale_revision"
    expect(peer.locator("#nativeError")).to_be_visible()
    expect(peer_lock).not_to_be_checked()
    assert preview(peer) == after
    peer.close()

    click_post(page, page.locator("#nativeUndo"), "/undo")
    assert preview(page) == before
    click_post(page, page.locator("#nativeRedo"), "/redo")
    assert preview(page) == after
    lock = page.get_by_role("checkbox", name=lock_label, exact=True)
    click_post(page, lock, "/commands")
    locked = click_post(page, page.locator("#nativeRecolor"), "/commands", 423)
    assert locked["detail"]["code"] == "locked"
    expect(page.locator("#nativeError")).to_be_visible()
    expect(lock).to_be_checked()
    assert preview(page) == after
    click_post(page, lock, "/commands")

    saved = click_post(page, page.locator("#nativeSave"), "/save")
    assert not saved["dirty"]
    # Loading the same URL reloads the document and drops the in-memory token.
    authenticate(page, url, token, reconnect=True)
    expect(page.locator("#nativeStatus")).to_contain_text("Saved · revision " + saved["revision"])
    assert preview(page) == after
    page.screenshot(path=output / "saved-editor.png", mask=[page.locator("#token")])

    replay = page.locator("#nativeReplay")
    replay.press("Home")
    expect(page.locator("#nativeReplayCount")).to_have_text("0")
    assert preview(page, output / "replay-zero.png") != after
    replay.press("End")
    expect(page.locator("#nativeReplayCount")).to_have_text(saved["stroke_count"])
    assert preview(page) == after

    draw = download(page, "Download saved .draw", output, "painting.draw")
    png = download(page, "Export painting PNG", output, "painting.png")
    ora = download(page, "Export painted layers ORA", output, "painting.ora")
    with zipfile.ZipFile(ora) as archive, Image.open(png) as exported:
        assert archive.read("mimetype") == b"image/openraster"
        merged = Image.open(io.BytesIO(archive.read("mergedimage.png"))).convert("RGBA")
        assert merged.size == exported.size
        assert merged.tobytes() == exported.convert("RGBA").tobytes()

    # Only the saved revision should survive replacement of the owning process.
    click_post(page, lock, "/commands")
    expect(page.locator("#nativeDownload")).to_be_disabled()
    restart()
    page.once("dialog", lambda dialog: dialog.accept())
    authenticate(page, url, token, reconnect=True)
    expect(page.locator("#nativeStatus")).to_contain_text("Saved · revision " + saved["revision"])
    expect(lock).not_to_be_checked()
    assert preview(page) == after
    click_post(page, page.locator("#nativeClose"), "/close")
    expect(page.locator("#nativeStatus")).to_have_text("No painting open")
    page.locator("#nativeFile").set_input_files(draw)
    imported = click_post(page, page.locator("#nativeImport"), "/import", 201)
    assert imported["id"] != saved["id"] and imported["strokes"] == saved["strokes"]
    assert preview(page) == after
    lineart = exercise_lineart(page, output / "lineart", url, token)
    mypaint = exercise_mypaint(page, output / "mypaint") if os.environ.get("DRAWBORAD_REQUIRE_MYPAINT")=="1" else {"requested":False}
    mypaint_poster = exercise_mypaint(page, output / "mypaint-poster",3300,5100) if os.environ.get("DRAWBORAD_REQUIRE_MYPAINT")=="1" else {"requested":False}
    changed_pixels = sum(before[i:i+3] != after[i:i+3] for i in range(0, len(after), 3))
    return {"project": saved["id"], "strokes": saved["stroke_count"], "stroke": stroke["id"],
            "changed_browser_pixels": changed_pixels, "saved_revision": saved["revision"],
            "stale_status": 409, "locked_status": 423, "exports": [draw.name, png.name, ora.name],
            "lineart": lineart, "mypaint": mypaint, "mypaint_poster": mypaint_poster}


def verify_in_chromium(source, output, url, token, restart, environment):
    output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 1440, "height": 1000}, reduced_motion="reduce")
        context.set_default_timeout(90000)
        page = context.new_page()
        try:
            receipt = exercise(context, page, source, output, url, token, restart)
            receipt.update(environment)
            receipt["browser"] = "Chromium " + browser.version
            (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
            print(json.dumps(receipt))
        except Exception:
            page.screenshot(path=output / "failure.png", mask=[page.locator("#token")], timeout=10000)
            raise
        finally:
            browser.close()


if __name__ == "__main__":
    if os.environ.get("COMPOSE_PROJECT_NAME") != "drawborad-native-ci":
        sys.exit("This acceptance recreates a container. Use the isolated drawborad-native-ci project.")
    source, output = (Path(arg).resolve() for arg in sys.argv[1:])
    url = "http://127.0.0.1:" + os.environ["DRAWBORAD_NATIVE_PORT"]

    def recreate_container():
        subprocess.run(["docker", "compose", "up", "-d", "--force-recreate", "--wait",
                        "--wait-timeout", "90", "editor"], check=True)

    verify_in_chromium(source, output, url, os.environ["DRAWBORAD_CLOUD_TOKEN"], recreate_container,
                       {"target": "packaged editor on CI loopback", "container_recovery": "saved revision"})
