# Real Qt software-window check (Linux, not Windows qualification)

The desktop CanvasItem, QtBridge and WorkflowController are now compiled together
in an optional Linux test target (`DRAWBORAD_QT_CANVAS_TESTS=ON`, requires headless
workflow plus official Qt Quick/Controls/Test development packages). The default
production Windows target and its Qt 6.12/D3D11 requirements are unchanged.

This surfaced existing compile defects in previously uncompiled desktop paths:
missing QJsonDocument/std::set includes, ambiguous QUrl empty-list assignment and
misleading single-line indentation under GCC -Werror. Fixed these without dropping
warnings or changing the intended behavior.

The real Qt software-rendered offscreen window test displays an imported red RGBA
layer, commits a native mouse stroke, checks pan/zoom leaves the document unchanged,
and verifies an unsupported transformed raster displays an error and blocks drawing.
It uses synthetic pixels only, never user reference art. The shared compositor
adapter also retains its independent byte-level tests.

Local Qt 6.8.2 test build passed, including both desktop tests and the full
95/95 CTest suite (106 seconds). The Linux CI installs official
qt6-declarative-dev and enables the same target so desktop-facing code is not left
uncompiled by ordinary Linux checks. Full Windows/D3D11 rendering, QML interface,
font layout and hardware performance remain unverified. The existing Windows
packaging workflow is manual, tied to its release branch and preinstalled codec SDK;
this change does not retarget that workflow or claim Windows release qualification.
