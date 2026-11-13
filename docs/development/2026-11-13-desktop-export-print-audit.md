# Desktop export / print truthfulness audit

The old desktop PSD action uses the event-only PaintedDocument path, limited to
4096 per axis and 4 megapixels plus a layer-memory guard. Thus a 3300 x 5100 poster
and imported raster layers are not supported by that action. Its limit is retained,
not silently raised, and failure now points to the supported alternatives.

The actual desktop QML now exposes full-committed-document PNG and streamed leaf
layer ORA exports, reusing the existing bounded headless/native implementations.
The full source snapshot is used, never a scaled display cache or preview. Output
paths require new filenames; failure leaves existing files unchanged. The source
snapshot is not mutated. Exports remain synchronous in this stage.

Both carry the document's pixel size and PPI. At 3300 x 5100 and 300 PPI the nominal
print size is 11 x 17 inches. The stored pixel dimensions are not evidence of new
native detail or professional artwork quality. No image upscaling is introduced.

Color is RGB in this path, not CMYK. Existing ICC bytes are carried when present;
this is not profile validation, color conversion or printer proofing. If no ICC is
available the success message explicitly warns that print color matching is not
assured. ORA preserves independent leaf pixel layers, visibility, offsets, opacity
and supported blend modes; group hierarchy is flattened and masks baked into
layer pixels. It is not full group/mask round-trip fidelity or a PSD substitute in
workflows requiring those structures. Photoshop/physical print/Windows unverified.

Also corrected PSD PPI encoding: its unsigned 16.16 resource cannot represent
100000 PPI; it now rejects out-of-range/zero-quantized values rather than wrapping.

## Local verification

Actual QML/WorkflowController exported a synthetic imported-lineart + native-stroke
3300 x 5100 document. QImage independently read the PNG at 299.9994 PPI (integer
pixels-per-meter rounding). Python zipfile/XML/Pillow read two independent nonempty
ORA layers (Normal and Multiply), 300 xres/yres, and verified RGBA byte equality of
PNG, ORA merged image and independent composition of this disjoint two-layer fixture.
The source snapshot was unchanged. An existing destination was rejected and its
bytes preserved. These engineering fixtures demonstrate data fidelity, not artwork
quality, Photoshop interoperability or physical printer output.

PSD PPI regression: C05 29 cases passed, including 16.16 minimum/maximum/fractional
round trips and invalid values leaving existing output unchanged. ZipStream's
private emit method was renamed emitBytes to avoid collision with Qt's emit macro
when the ORA writer is included by the desktop controller.

UI test coverage is full QML loading plus direct controller export calls, not
clicking a native OS file picker. Export always includes all committed events, not
the current replay prefix. ORA does not preserve lock state. The disjoint-layer
external comparison does not claim full coverage of overlapping blend semantics.
