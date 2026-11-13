# Desktop display parity: WIP, Windows unverified

Goal: imported RGBA lineart and cropped MyPaint layers must not disappear when a saved document opens in the desktop shell. This does not claim artistic quality or desktop feature parity.

Root cause: CanvasItem filtered layers through nativeLayerSupported, excluding imported RGBA sources, and composited each remaining layer independently. The desktop now uses the same bounded RGB8 merged renderer as headless/export for its display image. Source layers, original resources, events and saved data remain unchanged. The cached image updates on document/replay change, not on pan/zoom. Display textures remain tiled. Non-transformed supported documents therefore include RGBA, cropped layers, blend modes and masks as handled by that renderer.

Compatibility: transformed native-only scenes retain the old scene-graph path. Other unsupported or over-budget documents show an explicit error and disable drawing rather than silently displaying a partial page. Texture creation failure clears the partial frame and queues a document/revision/frame-guarded error on the UI thread.

Verification: portable Qt image adapter test covers imported ink, cropped Normal alpha, native-event replay, visibility, 512-pixel boundary, ownership, budgets and unsupported transforms; shared compositor output agrees byte-for-byte. Linux full suite passed 94/94 before final added replay cases; focused strengthened adapter test passed afterwards. A Windows Qt/D3D11 imported-raster window test was added but NOT RUN here.

Remaining: full Windows Qt 6.12/MSVC/D3D11 build and GUI verification; error UI interaction and texture allocation failure; large-document UI latency/performance. Full composite calculation currently runs synchronously on snapshot refresh. This is a draft implementation, not a Windows-qualified release. Other desktop drawing controls remain behind the web application.
