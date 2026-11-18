"""CLI entry point for the differentiable perceptual stroke planner."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

from .optimizer import DifferentiableArtistConfig, optimize_manifest_to_native


def _parse_size(text: str) -> tuple[int, int]:
    m = re.fullmatch(r'([1-9]\d{0,4})[xX]([1-9]\d{0,4})', text.strip())
    if not m:
        raise argparse.ArgumentTypeError('Expected WxH dimensions')
    w, h = int(m.group(1)), int(m.group(2))
    if w > 8192 or h > 8192 or w * h > 20_000_000:
        raise argparse.ArgumentTypeError('Dimensions exceed 8192 per axis / 20M pixels')
    return w, h


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--steps', type=int, default=40)
    parser.add_argument('--max-strokes', type=int, default=160)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--amodal-background', action='store_true')
    parser.add_argument('--no-perceptual', action='store_true')
    parser.add_argument('--target-size', type=_parse_size, default=None)
    parser.add_argument('--target-dpi', type=float, default=300.0)
    args = parser.parse_args(argv)

    cfg = DifferentiableArtistConfig(
        steps=args.steps,
        max_strokes_per_object=args.max_strokes,
        amodal_background=bool(args.amodal_background),
        use_perceptual_loss=not args.no_perceptual,
        target_size=args.target_size,
        target_dpi=args.target_dpi,
    )
    report = optimize_manifest_to_native(
        args.manifest, args.engine, args.output, config=cfg, allow_fixture=args.fixture
    )
    print(json.dumps(report))
    return 0


if __name__ == '__main__':
    sys.exit(main())
