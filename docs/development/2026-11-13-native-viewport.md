# Native web viewport: work in progress

Goal: make original-resolution strokes practically editable; a 2625px page previously fit into at most 560 CSS pixels. This changes viewing, not document resolution or artwork quality.

Implemented locally: Fit/50/100/200/400%, explicit no-paint pan mode, bounded translations, existing inverse image-rect coordinate mapping, viewport-sized overlays, view retention across same-document revisions, reset on new document. 100% means one document pixel per CSS pixel, not physical display pixel. Active drawing/hidden boundaries cancel on viewport escape/resize. Blur ends pan; unrelated pointers cannot end its gesture.

Validation: all 10 current UI CJS scripts passed, including new layout and actual pan handler tests. Independent review found boundary-outside-host and blur issues; both fixed. No backend/format changes.

NOT YET RELEASED: real browser integration test could not launch Chromium: process_singleton socket() Operation not permitted even with approved command execution. Test script /tmp/zoom-browser.cjs exercises isolated local server on 8896, 200% pan, actual native stroke commit, view preservation and blur recovery. Must complete browser test and mixed-paint/fill/eyedropper/hidden-boundary checks before release. No painting-quality claim. Qt desktop remains unchanged.

CI continuation: upload as draft PR WIP only. Existing Linux workflow already installs Playwright/Chromium; adds native_view_browser.py after that step using isolated synthetic canvas. Passing requires real 200% view, pan without edits, native stroke commit, retained view and blur recovery. Await exact-commit CI before calling browser validation passed. No private artwork is uploaded.
