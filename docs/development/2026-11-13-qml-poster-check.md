# Real QML / poster display checkpoint

The optional Linux Qt software-window regression now loads the actual `ui/Main.qml`,
checks that an unsupported raster transform displays the user-facing error label,
and checks that opening a supported document clears that label. CI explicitly
installs the QML runtime modules instead of relying on developer-machine leftovers.

A synthetic 3300 x 5100, 300 DPI project is then opened in this actual UI, displayed,
painted using mouse events (both event count and visible stroke pixels checked),
saved and reopened with source content equality checked.
The red synthetic area is an engineering fixture, **not finished artwork**.

This exposed visible grid seams at 512-pixel display-tile boundaries under the Qt
software renderer at fit-to-window scale. The software backend now displays its
already bounded composite as one texture/image; GPU backends retain texture tiles
to respect hardware limits. A 101 x 101 screenshot pixel region crosses former tile
boundaries and must remain solid red. No source layer is flattened or rewritten.

The first full poster UI journey passed locally in 38.9 seconds. This is functional
validation, not a claim of interactive speed. Rendering is still synchronous and
large-document responsiveness needs further work. Windows Qt 6.12 / D3D11, GPU tile
seams, hardware pen pressure, full UI coverage and final artwork quality remain
unverified. No user computer was accessed and no private artwork was uploaded.
