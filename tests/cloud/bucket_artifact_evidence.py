"""Pure-Pillow verification of native bucket export artifacts; no browser dependency."""
import io
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image


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

