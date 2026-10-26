"""E09: regression for distinct wood/saucer/cup/spoon visible-only layers.

Uses recorded fixtures already checked into the repository. No SAM execution,
model download, source photo, Qt, or Photoshop required.
"""
import io
from pathlib import Path
import tempfile
import unittest
import zipfile
from xml.etree import ElementTree as ET

import numpy as np
from PIL import Image

from tools.semantic_layers import (
    load_object_masks, make_rgba_layers, composite_layers, save_openraster,
)

FIXTURES = Path(__file__).resolve().parent / 'fixtures' / 'coffee-reviewed-scene'


def sample_rgb():
    yy, xx = np.indices((400, 600))
    return np.stack((xx % 256, yy % 256, (xx + yy) % 256), axis=2).astype('uint8')


class SemanticLayerIsolationTests(unittest.TestCase):
    def test_red_cup_body_is_not_in_red_saucer(self):
        masks = load_object_masks(FIXTURES)
        self.assertEqual(list(masks), ['wood_table', 'red_saucer', 'cup', 'spoon'])
        self.assertEqual([int(np.count_nonzero(x)) for x in masks.values()],
                         [127872, 54857, 50676, 6595])
        samples = [(250, 260, 'cup'), (420, 200, 'red_saucer'),
                   (370, 284, 'spoon'), (50, 45, 'wood_table'),
                   (350, 110, 'cup'), (208, 250, 'cup')]
        for x, y, intended in samples:
            for label, mask in masks.items():
                self.assertEqual(int(mask[y, x]), 255 if label == intended else 0)
        stacked = np.stack(list(masks.values()))
        self.assertTrue(np.all((stacked > 0).sum(axis=0) == 1))

    def test_cup_visibility_isolated_and_full_image_recomposes(self):
        rgb = sample_rgb()
        masks = load_object_masks(FIXTURES)
        layers = make_rgba_layers(rgb, masks)
        for label, pixels in layers.items():
            self.assertTrue(np.array_equal(pixels[:, :, 3], masks[label]))
            self.assertTrue(np.all(pixels[masks[label] == 0, :3] == 0))
        composite = composite_layers(layers)
        self.assertTrue(np.array_equal(composite[:, :, :3], rgb))
        self.assertTrue(np.all(composite[:, :, 3] == 255))
        without_cup = composite_layers({k: v for k, v in layers.items() if k != 'cup'})
        outside = masks['cup'] == 0
        self.assertTrue(np.array_equal(without_cup[outside], composite[outside]))
        self.assertFalse(np.any(without_cup[:, :, 3][~outside]))

    def test_openraster_roundtrip_keeps_four_distinct_pixel_layers(self):
        layers = make_rgba_layers(sample_rgb(), load_object_masks(FIXTURES))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'layers.ora'
            save_openraster(path, layers)
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.namelist()[0], 'mimetype')
                self.assertEqual(archive.read('mimetype'), b'image/openraster')
                self.assertEqual(archive.getinfo('mimetype').compress_type, zipfile.ZIP_STORED)
                self.assertIn('Thumbnails/thumbnail.png', archive.namelist())
                stack = ET.fromstring(archive.read('stack.xml')).find('./stack')
                self.assertNotIn('name', stack.attrib)
                rows = stack.findall('./layer')
                self.assertEqual(len(rows), 4)
                self.assertEqual({row.attrib['name'] for row in rows}, set(layers))
                for row in rows:
                    decoded = np.array(Image.open(io.BytesIO(archive.read(row.attrib['src']))).convert('RGBA'))
                    self.assertTrue(np.array_equal(decoded, layers[row.attrib['name']]))


if __name__ == '__main__':
    unittest.main()