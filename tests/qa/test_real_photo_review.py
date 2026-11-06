"""Behavioral checks for the real-photo human review board."""
import json
import tempfile
import unittest
from pathlib import Path
from PIL import Image
from tools.qa.real_photo_review import build_review


class ReviewBoardTests(unittest.TestCase):
    def make_fixture(self, root):
        p = Path(root)
        source = p / 'source.png'
        foreground = p / 'foreground.png'
        painted = p / 'painted.png'
        report = p / 'report.json'
        Image.new('RGB', (2, 1)).save(source)
        src = Image.open(source)
        src.putpixel((0, 0), (255, 0, 0))
        src.putpixel((1, 0), (0, 0, 255))
        src.save(source)
        Image.new('RGBA', (2, 1), (0, 0, 0, 0)).save(foreground)
        fg = Image.open(foreground)
        fg.putpixel((1, 0), (0, 0, 255, 255))
        fg.save(foreground)
        Image.new('RGB', (2, 1), (0, 0, 0)).save(painted)
        img = Image.open(painted)
        img.putpixel((0, 0), (255, 0, 0))
        img.save(painted)
        report.write_text(json.dumps({'strokeEvents': 4, 'objectCount': 2, 'backend': 'opencv-grabcut', 'replaySourceAssetReads': 0}))
        return source, foreground, painted, report

    def test_measures_rgb_error_and_foreground_coverage(self):
        with tempfile.TemporaryDirectory() as temp:
            source, foreground, painted, report = self.make_fixture(temp)
            output = Path(temp) / 'review.png'
            result = build_review(source, foreground, painted, report, output)
            self.assertAlmostEqual(result['normalizedRgbMae'], 1 / 6)
            self.assertEqual(result['foregroundPixels'], 1)
            self.assertAlmostEqual(result['foregroundFraction'], .5)
            self.assertEqual(result['strokeEvents'], 4)
            self.assertEqual(result['objectCount'], 2)
            self.assertEqual(result['backend'], 'opencv-grabcut')
            self.assertEqual(result['qualityNote'], 'Coverage is not semantic segmentation accuracy.')
            with Image.open(output) as image:
                self.assertEqual(image.format, 'PNG')
                self.assertGreater(image.width, 900)
                self.assertEqual(image.getpixel((110, 380)), (255, 0, 0), 'source preview should upscale to fill its card')
            self.assertEqual(json.loads(output.with_suffix('.json').read_text()), result)

    def test_rejects_dimension_mismatch_without_board(self):
        with tempfile.TemporaryDirectory() as temp:
            source, foreground, painted, report = self.make_fixture(temp)
            Image.new('RGBA', (3, 1), 'white').save(painted)
            output = Path(temp) / 'review.png'
            with self.assertRaisesRegex(ValueError, 'dimensions'):
                build_review(source, foreground, painted, report, output)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
