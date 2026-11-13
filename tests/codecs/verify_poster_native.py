"""Check native 11x17 inch / 300dpi technical poster, not enlarged artwork."""
import io
import sys
import zipfile
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops, ImageDraw

with zipfile.ZipFile(sys.argv[1]) as archive:
    assert archive.testzip() is None
    assert archive.read("mimetype") == b"image/openraster"
    root = ET.fromstring(archive.read("stack.xml"))
    assert (int(root.attrib["w"]), int(root.attrib["h"])) == (3300, 5100)
    layers = root.findall("./stack/layer")
    assert [x.attrib["name"] for x in layers] == ["Original lineart", "Color", "Paper"]
    assert layers[0].attrib["composite-op"] == "svg:multiply"
    merged = Image.open(io.BytesIO(archive.read("mergedimage.png")))
    assert merged.size == (3300, 5100)
    assert all(abs(v - 300) < .1 for v in merged.info["dpi"])
    composite = Image.new("RGBA", merged.size, (0, 0, 0, 0))
    for index in (2, 1, 0):
        layer = layers[index]
        image = Image.open(io.BytesIO(archive.read(layer.attrib["src"])))
        assert image.mode == "RGBA" and image.size == merged.size
        assert all(abs(v - 300) < .1 for v in image.info["dpi"])
        assert layer.attrib["opacity"] == "1" and layer.attrib["visibility"] == "visible"
        if index == 0:
            source = Image.new("RGBA", merged.size, "white")
            draw = ImageDraw.Draw(source)
            draw.line((1650, 0, 1650, 5099), fill=(0, 0, 0, 255), width=1)
            draw.line((1700, 0, 1700, 5099), fill=(128, 128, 128, 255), width=1)
            assert image.tobytes() == source.tobytes(), "Native single-pixel raw lineart must be retained"
            composite = ImageChops.multiply(composite, image)
        else:
            composite = Image.alpha_composite(composite, image)
    assert composite.tobytes() == merged.convert("RGBA").tobytes()
    expected = Image.new("RGBA", merged.size, (0, 255, 255, 255))
    draw = ImageDraw.Draw(expected)
    draw.line((1650, 0, 1650, 5099), fill=(0, 0, 0, 255), width=1)
    draw.line((1700, 0, 1700, 5099), fill=(0, 128, 128, 255), width=1)
    assert expected.tobytes() == composite.tobytes()
print("native poster 3300x5100 / 300dpi, 3 editable layers, single-pixel guides and independent recomposition passed")
