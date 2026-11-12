"""Focused real-DOM UI regression, with isolated API fixtures and no live owner token.

Run after installing the browser acceptance dependencies:
    python tests/cloud/native_layer_selection_acceptance.py
Use DRAWBORAD_TEST_CHROMIUM to select an existing Chromium executable.
The packaged native-engine acceptance remains browser_native_acceptance.py.
"""
import base64
from copy import deepcopy
import json
import os
from pathlib import Path
from urllib.parse import urlsplit

from playwright.sync_api import expect, sync_playwright

ROOT = Path(__file__).resolve().parents[2]
PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/a9sAAAAASUVORK5CYII=")
TOKEN = "isolated-browser-fixture-only"


def fixture():
    def node(id, name, kind, parent, owner="0"):
        return dict(id=id, name=name, kind=kind, parent_id=parent, object_id=owner,
                    stage="paint" if kind == 3 else "", visible=True, locked=False, opacity=1)

    nodes = [node("root", "Document", 0, "0"), node("cat", "Cat", 1, "root"),
             node("body", "Body", 3, "cat", "cat"), node("face", "Face", 2, "cat", "cat"),
             node("eyes", "Eyes <b>untrusted name</b>", 3, "face", "cat"),
             node("guides", "Guides", 3, "cat", "cat"), node("ball", "Ball", 1, "root"),
             node("ball-paint", "Ball paint", 3, "ball", "ball"),
             node("ball-empty", "Empty paint", 3, "ball", "ball")]
    strokes = [dict(id=id, layer_id=layer, object_id=owner, color=[.2, .3, .4, 1],
                    alpha=1, geometry_count="4", start_us="0", end_us="100", seed="1", tool="brush")
               for id, layer, owner in [("s1", "body", "cat"), ("s2", "body", "cat"),
                                        ("s3", "eyes", "cat"), ("s4", "ball-paint", "ball")]]
    return dict(id="00000000-0000-0000-0000-000000000001", revision="1", dirty=False,
                stroke_count="4", width="512", height="512", nodes=nodes, strokes=strokes)


def exercise(page):
    state = fixture()
    requests, page_errors = [], []
    failure = None
    page.on("pageerror", lambda error: page_errors.append(str(error)))

    def route(request_route):
        nonlocal state, failure
        request = request_route.request
        path = urlsplit(request.url).path
        if path == "/":
            request_route.fulfill(content_type="text/html", body=(ROOT / "cloud/static/index.html").read_text())
            return
        requests.append((request.method, path))
        if request.headers.get("authorization") != "Bearer " + TOKEN:
            request_route.fulfill(status=401, json={"detail": "Bearer token required"})
            return
        if path.endswith("/capabilities"):
            request_route.fulfill(json={"model_ready": False, "native_engine_configured": True})
        elif path.endswith("/preview.png"):
            request_route.fulfill(content_type="image/png", body=PNG)
        elif request.method == "POST" and path.endswith("/commands"):
            body = request.post_data_json
            assert body["expected_revision"] == state["revision"]
            if failure:
                status, code = failure
                failure = None
                request_route.fulfill(status=status, json={"detail": {"code": code, "message": code}})
                return
            if body["type"] == "recolor_stroke":
                assert body["stroke_id"] == "s1", "Layer selection must submit the chosen stable stroke ID"
                assert body["color"] == "#00ff00"
                state["strokes"][0]["color"] = [0, 1, 0, 1]
            else:
                assert set(body) == {"expected_revision", "type", "node_id", "locked"}
                assert body["type"] == "set_properties" and body["node_id"] == "cat"
                state["nodes"][1]["locked"] = body["locked"]
            state["revision"] = str(int(state["revision"]) + 1)
            state["dirty"] = True
            request_route.fulfill(json=deepcopy(state))
        elif request.method == "POST" and path.endswith("/close"):
            body = request.post_data_json
            assert body["expected_revision"] == state["revision"]
            if state["dirty"] and not body["discard"]:
                request_route.fulfill(status=409, json={"detail": {"code": "unsaved_changes", "message": "Save or discard changes"}})
            else:
                request_route.fulfill(json={})
        else:
            request_route.fulfill(json=deepcopy(state), status=201 if request.method == "POST" else 200)

    page.route("**/*", route)
    page.goto("http://drawborad.test/")
    expect(page.locator("#deviceStatus")).to_have_text("Owner access token required")
    field = page.get_by_label("Access token", exact=True)
    field.fill(TOKEN)
    field.press("Tab")
    page.locator("#nativeNew").click()
    expect(page.locator("#nativeNew")).to_be_enabled()
    expect(page.locator("#nativeStrokes option")).to_have_count(4)
    expect(page.locator("#nativeLayerSummary")).to_contain_text("2 objects")
    expect(page.locator("#nativeLayerSummary")).to_contain_text("5 layers")
    expect(page.locator("#nativeLayerSummary")).to_contain_text("0 raster")
    expect(page.locator("#nativeLayerSummary")).to_contain_text("2 empty native layers")
    expect(page.locator("#nativeNodes b")).to_have_count(0)
    assert page.locator("#nativeNodes").evaluate("e => parseFloat(getComputedStyle(e).maxHeight)") >= 360

    def select(id, want):
        button = page.locator('[data-node-id="' + id + '"] .native-node-select')
        before = len(requests)
        button.click()
        expect(button).to_have_attribute("aria-pressed", "true")
        assert page.locator("#nativeStrokes option").evaluate_all("es => es.map(e => e.value)") == want
        assert len(requests) == before, "Selecting a layer must not mutate or refetch the native painting"
        return button

    expect(page.locator('[data-node-id="body"]')).to_be_hidden()
    expect(page.locator('[data-node-id="cat"]')).to_be_visible()
    page.locator("#nativeShowSublayers").check()
    body = select("body", ["s1", "s2"])
    expect(page.locator("#nativeScope")).to_contain_text("Body")
    expect(page.locator("#nativeScope")).to_contain_text("2 of 4 strokes")
    page.locator("#nativeStrokes").select_option("s1")
    page.locator("#nativeRecolor").click()
    expect(page.locator("#nativeNew")).to_be_enabled()
    expect(body).to_have_attribute("aria-pressed", "true")
    expect(page.locator("#nativeStrokes")).to_have_value("s1")
    expect(page.locator("#nativeDownload")).to_be_disabled()
    expect(page.locator("#nativeShowSublayers")).to_be_checked()
    select("cat", ["s1", "s2", "s3"])
    select("face", ["s3"])
    select("eyes", ["s3"])
    page.locator("#nativeShowSublayers").uncheck()
    expect(page.locator('[data-node-id="cat"] .native-node-select')).to_have_attribute("aria-pressed", "true")
    expect(page.locator('[data-node-id="eyes"]')).to_be_hidden()
    page.locator("#nativeShowSublayers").check()
    select("guides", [])
    for id in ("nativeStrokes", "nativeRecolor", "nativeColor"):
        expect(page.locator("#" + id)).to_be_disabled()
    expect(page.locator("#nativeReplay")).to_be_enabled()
    expect(page.locator("#nativeStrokeInfo")).to_contain_text("No brush events in this selection")
    select("ball-paint", ["s4"])
    page.locator("#nativeAllStrokes").click()
    expect(page.locator("#nativeStrokes option")).to_have_count(4)
    expect(page.locator("#nativeAllStrokes")).to_have_attribute("aria-pressed", "true")
    expect(page.locator("#nativeStrokes")).to_have_value("s4")

    # Keyboard selection, state-refresh errors, and sibling controls remain independent.
    body.focus()
    page.keyboard.press("Enter")
    expect(body).to_have_attribute("aria-pressed", "true")
    page.locator("#nativeStrokes").select_option("s1")
    lock = page.get_by_role("checkbox", name="Cat locked", exact=True)
    lock.check()
    expect(page.locator("#nativeNew")).to_be_enabled()
    expect(body).to_have_attribute("aria-pressed", "true")
    for status, code in [(423, "locked"), (409, "stale_revision")]:
        failure = status, code
        page.locator("#nativeRecolor").click()
        expect(page.locator("#nativeError")).to_contain_text(code)
        expect(page.locator("#nativeNew")).to_be_enabled()
        expect(page.locator("#nativeStrokes")).to_have_value("s1")
        expect(body).to_have_attribute("aria-pressed", "true")
        expect(lock).to_be_checked()
    page.locator("#nativeClose").click()
    expect(page.locator("#nativeError")).to_contain_text("Save or discard")
    expect(body).to_have_attribute("aria-pressed", "true")
    page.locator("#nativeDiscard").check()
    page.locator("#nativeClose").click()
    expect(page.locator("#nativeStatus")).to_have_text("No painting open")
    expect(page.locator("#nativeScope")).to_have_text("No layer selected")
    expect(page.locator("#nativeLayerSummary")).to_have_text("")
    expect(page.locator("#nativeReplayCount")).to_have_text("0")
    expect(page.locator("#nativeAllStrokes")).to_be_disabled()
    expect(page.locator("#nativeNodes")).to_be_empty()

    # Reused native node IDs across projects must not retain a previous scope.
    state = fixture()
    state["id"] = "00000000-0000-0000-0000-000000000002"
    page.locator("#nativeNew").click()
    expect(page.locator("#nativeNew")).to_be_enabled()
    expect(page.locator("#nativeStrokes option")).to_have_count(4)
    expect(page.locator("#nativeAllStrokes")).to_have_attribute("aria-pressed", "true")
    select("body", ["s1", "s2"])
    state = fixture()
    state["id"] = "00000000-0000-0000-0000-000000000003"
    page.locator("#nativeNew").click()
    expect(page.locator("#nativeNew")).to_be_enabled()
    expect(page.locator("#nativeStrokes option")).to_have_count(4)
    expect(page.locator("#nativeAllStrokes")).to_have_attribute("aria-pressed", "true")

    # The responsive panel does not force narrow screens to scroll sideways.
    page.set_viewport_size({"width": 390, "height": 844})
    assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth")
    assert not page_errors, page_errors
    return {"checks": "layer/object/group scope, counts, empty selection, recolor IDs, keyboard, error recovery, close/switch, responsive layout", "requests": len(requests)}


if __name__ == "__main__":
    with sync_playwright() as playwright:
        executable = os.environ.get("DRAWBORAD_TEST_CHROMIUM")
        browser = playwright.chromium.launch(**({"executable_path": executable} if executable else {}))
        try:
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            page.set_default_timeout(5000)
            print(json.dumps(exercise(page)))
        finally:
            browser.close()
