#!/usr/bin/env python3
"""Developer-only SAM 2.1 ONNX export tool.

This tool is not part of the Drawborad runtime. It requires a local checkout
of Meta's SAM 2 repository and a local checkpoint. It never downloads files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git_commit(root: pathlib.Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("source checkout has no readable git commit")
    value = result.stdout.strip()
    if len(value) != 40:
        raise RuntimeError("source commit is not a 40-character SHA")
    return value


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Export a local SAM 2.1 image encoder and prompt decoder to ONNX."
    )
    p.add_argument("--source", type=pathlib.Path, required=True,
                   help="Local facebookresearch/sam2 checkout.")
    p.add_argument("--checkpoint", type=pathlib.Path, required=True,
                   help="Local SAM 2.1 .pt checkpoint.")
    p.add_argument("--variant", choices=("tiny", "small"), required=True)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--opset", type=int, default=17)
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    source = args.source.resolve()
    checkpoint = args.checkpoint.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise SystemExit("--source must be a local directory")
    if not checkpoint.is_file():
        raise SystemExit("--checkpoint must be a local file")
    if args.opset < 17:
        raise SystemExit("--opset must be >= 17")

    # Heavy developer dependencies load only after argparse. This keeps
    # --help usable in build/test environments that do not have torch.
    try:
        import torch
        import onnx
    except Exception as exc:  # pragma: no cover - dependency-specific
        raise SystemExit(f"developer dependencies unavailable: {exc}") from exc

    sys.path.insert(0, str(source))
    try:
        from sam2.build_sam import build_sam2
    except Exception as exc:  # pragma: no cover - upstream-specific
        raise SystemExit(f"cannot import local SAM 2 source: {exc}") from exc

    config = (
        "configs/sam2.1/sam2.1_hiera_t.yaml"
        if args.variant == "tiny"
        else "configs/sam2.1/sam2.1_hiera_s.yaml"
    )
    model = build_sam2(
        config,
        str(checkpoint),
        device="cpu",
        apply_postprocessing=False,
    ).eval()

    class Encoder(torch.nn.Module):
        def __init__(self, sam):
            super().__init__()
            self.sam = sam

        def forward(self, image):
            backbone = self.sam.forward_image(image)
            _, features, _, _ = self.sam._prepare_backbone_features(backbone)
            if self.sam.directly_add_no_mem_embed:
                features[-1] = features[-1] + self.sam.no_mem_embed
            maps = [
                feature.permute(1, 2, 0).reshape(1, -1, *size)
                for feature, size in zip(
                    features, ((256, 256), (128, 128), (64, 64))
                )
            ]
            return maps[-1], maps[0], maps[1]

    class Decoder(torch.nn.Module):
        def __init__(self, sam):
            super().__init__()
            self.prompt = sam.sam_prompt_encoder
            self.mask = sam.sam_mask_decoder

        def forward(
            self,
            image_embed,
            high_res_0,
            high_res_1,
            point_coords,
            point_labels,
            mask_input,
            has_mask_input,
        ):
            sparse = self.prompt._embed_points(point_coords, point_labels, pad=True)
            flag = has_mask_input.reshape(1, 1, 1, 1)
            dense = (
                self.prompt._embed_masks(mask_input) * flag
                + self.prompt.no_mask_embed.weight.reshape(1, -1, 1, 1)
                .expand(1, -1, 64, 64)
                * (1 - flag)
            )
            masks, scores, _, _ = self.mask(
                image_embeddings=image_embed,
                image_pe=self.prompt.get_dense_pe(),
                sparse_prompt_embeddings=sparse,
                dense_prompt_embeddings=dense,
                multimask_output=True,
                repeat_image=False,
                high_res_features=[high_res_0, high_res_1],
            )
            return masks, scores

    output.mkdir(parents=True, exist_ok=True)
    encoder_path = output / "encoder.onnx"
    decoder_path = output / "decoder.onnx"
    image = torch.zeros(1, 3, 1024, 1024, dtype=torch.float32)
    encoder = Encoder(model).eval()
    decoder = Decoder(model).eval()

    with torch.no_grad():
        features = encoder(image)
    decoder_inputs = (
        *features,
        torch.tensor([[[512.0, 512.0]]], dtype=torch.float32),
        torch.tensor([[1]], dtype=torch.int64),
        torch.zeros(1, 1, 256, 256, dtype=torch.float32),
        torch.zeros(1, dtype=torch.float32),
    )

    torch.onnx.export(
        encoder,
        (image,),
        str(encoder_path),
        input_names=["image"],
        output_names=["image_embed", "high_res_0", "high_res_1"],
        opset_version=args.opset,
        dynamo=False,
        external_data=False,
    )
    torch.onnx.export(
        decoder,
        decoder_inputs,
        str(decoder_path),
        input_names=[
            "image_embed", "high_res_0", "high_res_1",
            "point_coords", "point_labels", "mask_input", "has_mask_input",
        ],
        output_names=["low_res_masks", "iou_scores"],
        dynamic_axes={"point_coords": {1: "N"}, "point_labels": {1: "N"}},
        opset_version=args.opset,
        dynamo=False,
        external_data=False,
    )
    onnx.checker.check_model(str(encoder_path))
    onnx.checker.check_model(str(decoder_path))

    manifest = {
        "schema": 1,
        "taskId": "sam21.image.v1",
        "variant": args.variant,
        "sourceCommit": git_commit(source),
        "checkpointSha256": sha256_file(checkpoint),
        "opset": args.opset,
        "encoder": {
            "file": encoder_path.name,
            "sha256": sha256_file(encoder_path),
        },
        "decoder": {
            "file": decoder_path.name,
            "sha256": sha256_file(decoder_path),
        },
        "logicalInputs": [
            {"name": "image", "dtype": "float32", "shape": [1, 3, 1024, 1024]},
            {"name": "point_coords", "dtype": "float32", "shape": [1, -1, 2]},
            {"name": "point_labels", "dtype": "int64", "shape": [1, -1]},
            {"name": "mask_input", "dtype": "float32", "shape": [1, 1, 256, 256]},
            {"name": "has_mask_input", "dtype": "float32", "shape": [1]},
        ],
        "outputs": ["low_res_masks", "iou_scores"],
        "networkAccess": False,
    }
    (output / "export-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
