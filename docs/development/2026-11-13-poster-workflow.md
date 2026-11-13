# Native poster workflow integration

This consolidates previously separate technical checks on one actual 3300×5100 document. It is not a finished illustration or proof of artistic quality, CMYK output, or real detail reconstructed from a low-resolution source.

The real native API test creates a closed smooth-ink outline, fills a separate layer, recolors it without altering the ink/exterior, undoes a mistaken line and redraws it, then saves and restores recovery after server restart. Native PNG dimensions, ORA dimensions/layers/merged pixels and restored events are checked. Initial testing exposed an incorrect test assumption that blank pixels were opaque white; a new native canvas is transparent, so exterior and boundary pixels are compared against the actual before-image instead.

The existing real Chromium viewport test now has an opt-in `--poster` variant. It keeps its pan/no-paint, zoom coordinate, temporary draft, layer order and snapshot checks, then draws a closed path with actual pointer gestures at 200%, fills through the real page, corrects a line using Undo/redraw, saves a separate copy and opens the downloaded recovery file. The old small-canvas variant remains. Local native API test passed; local Chromium launch is blocked by host socket permissions, so the added CI run is required before browser acceptance is claimed.

Limits: synthetic marks only; mouse rather than physical tablet; Undo/redraw is not direct vector-point editing; this does not test CMYK print matching, Windows desktop parity, or persistence after loss of the entire cloud workspace.
