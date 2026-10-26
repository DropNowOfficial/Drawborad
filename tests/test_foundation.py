import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from tools.foundation_probe import (
    build_partition, plan_events, render_events, save_project, load_project,
    extract_prompt_region,
)


class FoundationTests(unittest.TestCase):
    def setUp(self):
        self.image = np.zeros((12, 16, 3), np.uint8)
        self.image[:, :8] = [250, 40, 30]
        self.image[:, 8:] = [20, 100, 230]
        self.masks = {
            'left': np.zeros((12, 16), np.uint8),
            'right': np.zeros((12, 16), np.uint8),
        }
        self.masks['left'][:, :8] = 255
        self.masks['right'][:, 8:] = 255

    def test_partition_fails_on_overlaps_and_unassigned_pixels(self):
        mask = np.ones((12, 16), np.uint8) * 255
        with self.assertRaisesRegex(ValueError, 'overlap'):
            build_partition([('a', mask), ('b', mask)], 16, 12, resolve_overlap=False)
        with self.assertRaisesRegex(ValueError, 'unassigned'):
            build_partition([('a', self.masks['left'])], 16, 12, include_background=False)

    def test_real_stroke_events_render_without_source_image(self):
        masks = build_partition(list(self.masks.items()), 16, 12, include_background=False)
        events = plan_events(self.image, masks, coarse=8, detail=4)
        self.assertGreater(len(events), 4)
        self.assertEqual({e['stage'] for e in events}, {'base', 'detail'})
        self.assertTrue(all('pixels' not in e and 'source' not in e for e in events))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            original = folder / 'temporary-source.png'
            Image.fromarray(self.image).save(original)
            save_project(folder / 'project', masks, events, 16, 12)
            original.unlink()
            restored_masks, restored_events, w, h = load_project(folder / 'project')
            blank = render_events([], restored_masks, w, h)
            final = render_events(restored_events, restored_masks, w, h)
            self.assertTrue(np.all(blank[:, :, 3] == 0))
            self.assertTrue(np.all(final[:, :, 3] == 255))
            self.assertTrue(np.array_equal(final[:, :8, :3], self.image[:, :8]))
            self.assertTrue(np.array_equal(final[:, 8:, :3], self.image[:, 8:]))
            self.assertTrue(np.array_equal(final, render_events(restored_events, restored_masks, w, h)))

    def test_changing_left_strokes_does_not_touch_right_layer(self):
        masks = build_partition(list(self.masks.items()), 16, 12, include_background=False)
        events = plan_events(self.image, masks, coarse=8, detail=4)
        first = render_events(events, masks, 16, 12)
        changed = [dict(e) for e in events]
        for e in changed:
            if e['object'] == 'left':
                e['color'] = [1, 220, 1]
        second = render_events(changed, masks, 16, 12)
        self.assertFalse(np.array_equal(first[:, :8], second[:, :8]))
        self.assertTrue(np.array_equal(first[:, 8:], second[:, 8:]))

    def test_guided_segmentation_handles_out_of_bounds_rectangle(self):
        with self.assertRaisesRegex(ValueError, 'bounds'):
            extract_prompt_region(self.image, (0, 0, 40, 20))


if __name__ == '__main__':
    unittest.main()