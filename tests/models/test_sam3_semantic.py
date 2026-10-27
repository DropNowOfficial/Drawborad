import json
import hashlib
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from tools.models.sam3_semantic import Candidate, build_semantic_partition, write_semantic_manifest, infer_candidates


def candidate(name, arr, score=0.8):
    return Candidate(name, score, np.asarray(arr, dtype=bool))


def test_frontmost_instance_wins_and_background_is_explicit():
    table = np.ones((5, 6), bool)
    saucer = np.zeros((5, 6), bool); saucer[2:5, 1:5] = True
    cup = np.zeros((5, 6), bool); cup[1:4, 2:4] = True
    spoon = np.zeros((5, 6), bool); spoon[0:3, 4:5] = True
    layers = build_semantic_partition([candidate("wooden table", table), candidate("red saucer", saucer), candidate("red cup", cup), candidate("spoon", spoon)], (5, 6))
    assert [x.name for x in layers] == ["wooden table", "red saucer", "red cup", "spoon"]
    coverage = np.stack([x.mask for x in layers]).sum(axis=0)
    assert np.all(coverage == 1)
    assert layers[0].mask[1, 2] == 0
    assert layers[2].mask[1, 2] == 1
    assert layers[1].mask[4, 3] == 1
    assert layers[3].mask[1, 4] == 1


def test_unknown_pixels_go_to_explicit_background_not_an_arbitrary_object():
    cup = np.zeros((3, 4), bool); cup[1, 1] = True
    layers = build_semantic_partition([candidate("cup", cup)], (3, 4))
    assert [x.name for x in layers] == ["Background (unrecognized)" ,"cup"]
    assert layers[0].mask.sum() == 11
    assert layers[1].mask.sum() == 1


def test_inference_preserves_text_labels_and_emits_instances():
    class FakeProcessor:
        def set_image(self, img):
            return {"image": img}
        def set_text_prompt(self, state, prompt):
            mask = np.zeros((1, 1, 3, 4), dtype=np.uint8)
            mask[0, 0, 0 if prompt == "cup" else 2, 1] = 1
            return {"masks":mask,"scores":np.array([0.91],dtype=np.float32)}
    image = Image.fromarray(np.zeros((3, 4, 3),dtype=np.uint8))
    detections = infer_candidates(FakeProcessor(), image, ["cup", "spoon"], min_score=0.5, min_pixels=1)
    assert [x.name for x in detections] == ["cup", "spoon"]
    assert abs(detections[0].score - 0.91) < 1e-5
    assert detections[1].mask[2, 1]


def test_manifest_preserves_source_hash_and_separate_mask_hashes(tmp_path):
    src = tmp_path / "input.png"
    Image.fromarray(np.full((3, 4, 3), 127, dtype=np.uint8)).save(src)
    cup = np.zeros((3, 4),bool); cup[1,1] = True
    layers = build_semantic_partition([candidate("cup",cup)], (3,4))
    output = tmp_path / "export"
    manifest_path = write_semantic_manifest(src, output, layers, "sam3-image", ["cup"])
    manifest = json.loads(manifest_path.read_text())
    assert manifest["schema"] == 1
    assert manifest["sourceSha256"] == hashlib.sha256(src.read_bytes()).hexdigest()
    assert manifest["width"] == 4 and manifest["height"] == 3
    assert [o["name"] for o in manifest["objects"]] == ["Background (unrecognized)", "cup"]
    assert manifest["objects"][0]["isBackground"]
    assert not manifest["objects"][1]["isBackground"]
    total=np.zeros((3,4), dtype=int)
    for o in manifest["objects"]:
        p=output/o["mask"]
        contents=p.read_bytes()
        assert len(contents) == 12
        assert hashlib.sha256(contents).hexdigest()==o["sha256"]
        total += np.frombuffer(contents, dtype=np.uint8).reshape((3,4))//255
    assert np.all(total == 1)
    assert (output/manifest["image"]).read_bytes()==src.read_bytes()


def test_refuses_empty_or_invalid_inputs(tmp_path):
    with pytest.raises(ValueError):
        build_semantic_partition([], (0, 4))
    with pytest.raises(ValueError):
        build_semantic_partition([candidate("bad", np.zeros((2,2)))], (3, 4))
    with pytest.raises(ValueError):
        build_semantic_partition([candidate("", np.ones((3,4)))], (3, 4))


def test_highest_confidence_instance_wins_same_prompt_overlap():
    class TwoCups:
        def set_image(self, image):
            return {"image": image}
        def set_text_prompt(self, state, prompt):
            mask = np.zeros((2, 1, 3, 4), dtype=np.float32)
            mask[0, 0, 1, 1] = 1  # strong detection
            mask[1, 0, 1, 1:3] = 1  # weaker detection overlaps
            return {"masks": mask, "scores": np.asarray([0.95, 0.65])}
    image = Image.fromarray(np.zeros((3, 4, 3), dtype=np.uint8))
    detections = infer_candidates(TwoCups(), image, ["cup"], min_pixels=1)
    layers = build_semantic_partition(detections, (3, 4))
    by_name = {x.name: x.mask for x in layers}
    assert by_name["cup 1"][1, 1]
    assert not by_name["cup 2"][1, 1]
    assert by_name["cup 2"][1, 2]


def test_export_rejects_missing_visible_pixels_before_writing(tmp_path):
    source = tmp_path / "source.png"
    Image.fromarray(np.zeros((3, 4, 3), dtype=np.uint8)).save(source)
    partial = np.zeros((3, 4), dtype=bool)
    partial[0, 0] = True
    out = tmp_path / "incomplete"
    with pytest.raises(ValueError, match="cover"):
        write_semantic_manifest(source, out, [candidate("cup", partial)], "sam3-image", ["cup"])
    assert not out.exists()


def test_export_rejects_inconsistent_backend_before_writing(tmp_path):
    source = tmp_path / "source.png"
    Image.fromarray(np.zeros((2, 2, 3), dtype=np.uint8)).save(source)
    with pytest.raises(ValueError, match="backend"):
        write_semantic_manifest(source, tmp_path / "wrong-backend",
                                [candidate("all", np.ones((2, 2), bool))],
                                "sam3", ["all"])


def test_export_refuses_partial_alpha_that_native_reconstruction_cannot_preserve(tmp_path):
    source = tmp_path / "alpha.png"
    pixels = np.zeros((2, 2, 4), dtype=np.uint8)
    pixels[:, :, :3] = 127
    pixels[:, :, 3] = 255
    pixels[0, 0, 3] = 128
    Image.fromarray(pixels, mode="RGBA").save(source)
    out = tmp_path / "bad-alpha"
    with pytest.raises(ValueError, match="partial alpha"):
        write_semantic_manifest(source, out,
                                [candidate("foreground", np.ones((2, 2), bool))],
                                "sam3-image", ["foreground"])
    assert not out.exists()