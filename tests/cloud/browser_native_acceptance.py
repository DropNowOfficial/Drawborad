"""Drive the packaged native editor in Chromium on an isolated Compose host."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from urllib.parse import urlsplit
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image
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
    assert response.status == status, f"{suffix} returned {response.status}"
    expect(page.locator("#nativeNew")).to_be_enabled(timeout=90000)
    return response.json()


def preview(page, destination=None):
    image = page.get_by_alt_text("Native painting preview")
    image.evaluate("(image) => image.decode()")
    png = image.screenshot(path=destination, animations="disabled")
    return Image.open(io.BytesIO(png)).convert("RGB").tobytes()


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


def verify_bucket_artifacts(ora, png, expected_layers, expected_size):
    """Check downloaded bytes, not screenshots or an export status response."""
    with zipfile.ZipFile(ora) as archive, Image.open(png) as exported:
        assert archive.read("mimetype") == b"image/openraster"
        layers = ET.fromstring(archive.read("stack.xml")).findall(".//layer")
        assert len(layers) == expected_layers
        fill_layers = [layer for layer in layers if layer.get("name") == "Color fill"]
        assert len(fill_layers) == 1
        fill = fill_layers[0]
        assert fill.get("visibility", "visible") == "visible"
        assert float(fill.get("opacity", "1")) == 1
        assert int(fill.get("x", "0")) == int(fill.get("y", "0")) == 0
        pixels = Image.open(io.BytesIO(archive.read(fill.get("src")))).convert("RGBA")
        merged = Image.open(io.BytesIO(archive.read("mergedimage.png"))).convert("RGBA")
        exported = exported.convert("RGBA")
        assert pixels.size == merged.size == exported.size == expected_size
        assert merged.tobytes() == exported.tobytes()
        assert pixels.tobytes() == exported.tobytes(), "Opaque top fill must equal the merged export"
        extrema = pixels.getextrema()
        assert all(low == high for low, high in extrema), "Tolerance 255 must fill the complete canvas"
        assert extrema[3] == (255, 255)
        return {"layers": len(layers), "fill_pixels": pixels.width * pixels.height,
                "rgba": [low for low, high in extrema], "merged_png_equal": True}


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
    bucket = exercise_bucket(page, imported, draw, ora, output / "bucket", url, token)
    changed_pixels = sum(before[i:i+3] != after[i:i+3] for i in range(0, len(after), 3))
    return {"project": saved["id"], "strokes": saved["stroke_count"], "stroke": stroke["id"],
            "changed_browser_pixels": changed_pixels, "saved_revision": saved["revision"],
            "stale_status": 409, "locked_status": 423, "exports": [draw.name, png.name, ora.name],
            "bucket": bucket}


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
