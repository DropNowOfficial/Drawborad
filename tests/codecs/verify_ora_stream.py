"""Independent streamed ORA fixture verification (not user artwork)."""
import io
import sys
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image, ImageChops

with zipfile.ZipFile(sys.argv[1]) as archive:
    assert archive.testzip() is None
    assert archive.namelist()[0] == "mimetype"
    assert archive.getinfo("mimetype").compress_type == zipfile.ZIP_STORED
    assert not archive.getinfo("mimetype").flag_bits & 8
    assert archive.read("mimetype") == b"image/openraster"
    root = ET.fromstring(archive.read("stack.xml"))
    size = (int(root.attrib["w"]), int(root.attrib["h"]))
    assert size == (1725, 2625)
    assert float(root.attrib["xres"]) == float(root.attrib["yres"]) == 300
    layers = root.findall("./stack/layer")
    assert len(layers) == 3
    assert layers[0].attrib["name"] == "Lineart & <original>"
    assert layers[0].attrib["composite-op"] == "svg:multiply"
    assert layers[1].attrib["visibility"] == "hidden"
    assert float(layers[1].attrib["opacity"]) == .375
    composite = Image.new("RGBA", size, (0, 0, 0, 0))
    for layer in reversed(layers):
        image = Image.open(io.BytesIO(archive.read(layer.attrib["src"])))
        assert image.size == size and image.mode == "RGBA"
        assert all(abs(dpi - 300) < .1 for dpi in image.info["dpi"])
        if len(sys.argv) > 2:
            assert image.info["icc_profile"] == open(sys.argv[2], "rb").read()
        if layer.attrib["visibility"] == "hidden":
            continue
        assert float(layer.attrib["opacity"]) == 1
        if layer.attrib["composite-op"] == "svg:multiply":
            composite = ImageChops.multiply(composite, image)
        else:
            composite = Image.alpha_composite(composite, image)
    merged = Image.open(io.BytesIO(archive.read("mergedimage.png"))).convert("RGBA")
    assert merged.size == size and composite.tobytes() == merged.tobytes()
    assert merged.getpixel((0, 0)) == (0, 0, 0, 255)
    assert merged.getpixel((100, 100)) == (0, 255, 255, 255)
    thumb = Image.open(io.BytesIO(archive.read("Thumbnails/thumbnail.png")))
    assert max(thumb.size) <= 256
print("independent ORA ZIP/PNG/layer recomposition/Multiply/visibility/opacity/300dpi verification passed")
