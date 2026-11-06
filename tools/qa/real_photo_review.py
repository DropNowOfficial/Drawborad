"""Read-only visual QA summary for real-photo C++ painting runs.

Creates one human-review board and adjacent metrics JSON from already-produced
source, cutout and painted artifacts. Does not run segmentation or painting.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFont


def _font(size):
    try:
        return ImageFont.truetype('DejaVuSans.ttf', size)
    except OSError:
        return ImageFont.load_default()


def _thumbnail(source, width, height):
    image = source.copy()
    scale = min(width / image.width, height / image.height)
    target = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    image = image.resize(target, Image.Resampling.NEAREST)
    thumb = Image.new('RGBA', (width, height), '#F1F3F6')
    if 'A' in image.getbands():
        for y in range(0, height, 16):
            for x in range(0, width, 16):
                if (x // 16 + y // 16) % 2:
                    ImageDraw.Draw(thumb).rectangle((x, y, x + 15, y + 15), fill='#E5E8EF')
        thumb.alpha_composite(image.convert('RGBA'), ((width - image.width) // 2, (height - image.height) // 2))
    else:
        thumb.paste(image.convert('RGBA'), ((width - image.width) // 2, (height - image.height) // 2))
    return thumb.convert('RGB')


def build_review(source, foreground, painted, report, output):
    """Measure RGB error, count nonzero cutout alpha and produce a visual board.

    Output is a diagnostic for human review, not a segmentation quality score.
    """
    source, foreground, painted, report, output = map(Path, (source, foreground, painted, report, output))
    with Image.open(source) as im:
        original = im.copy()
    with Image.open(foreground) as im:
        cutout = im.convert('RGBA')
    with Image.open(painted) as im:
        result = im.copy()
    if original.size != cutout.size or original.size != result.size:
        raise ValueError('source, foreground and painted image dimensions must match')
    width, height = original.size
    if not width or not height:
        raise ValueError('image dimensions must be nonzero')
    metadata = json.loads(report.read_text(encoding='utf-8'))
    difference = ImageChops.difference(original.convert('RGB'), result.convert('RGB'))
    histogram = difference.histogram()
    total = sum((index % 256) * count for index, count in enumerate(histogram))
    rgb_mae = total / (width * height * 3 * 255)
    alpha_histogram = cutout.getchannel('A').histogram()
    foreground_pixels = width * height - alpha_histogram[0]
    summary = {
        'width': width,
        'height': height,
        'normalizedRgbMae': rgb_mae,
        'foregroundPixels': foreground_pixels,
        'foregroundFraction': foreground_pixels / (width * height),
        'strokeEvents': metadata['strokeEvents'],
        'objectCount': metadata['objectCount'],
        'backend': metadata['backend'],
        'replaySourceAssetReads': metadata['replaySourceAssetReads'],
        'qualityNote': 'Coverage is not semantic segmentation accuracy.',
        'visualStatus': 'Needs human review',
    }
    board = Image.new('RGB', (1600, 812), '#F5F6F8')
    draw = ImageDraw.Draw(board)
    draw.text((72, 45), 'Drawborad  /  Real-photo review', fill='#15171A', font=_font(44))
    draw.text((74, 115), f'{width} x {height} px  |  {metadata["backend"]}  |  Needs human review', fill='#69707B', font=_font(23))
    images = [(original, '01  ORIGINAL PHOTO'), (cutout, '02  FOREGROUND CUTOUT'), (result, '03  C++ PAINTED OUTPUT')]
    for index, (image, label) in enumerate(images):
        left = 69 + index * 506
        draw.rounded_rectangle((left, 185, left + 468, 621), radius=22, fill='#FFFFFF', outline='#E2E6EC', width=2)
        draw.text((left + 24, 209), label, fill='#007AFF', font=_font(23))
        thumbnail = _thumbnail(image, 422, 284)
        board.paste(thumbnail, (left + 23, 262))
    draw.rounded_rectangle((69, 650, 1537, 754), radius=16, fill='#FFFFFF', outline='#E2E6EC', width=2)
    draw.text((98, 664), f'{summary["strokeEvents"]:,} strokes', fill='#191B1E', font=_font(27))
    draw.text((440, 664), f'{summary["objectCount"]} objects', fill='#191B1E', font=_font(27))
    draw.text((750, 664), f'RGB MAE {rgb_mae:.6f}', fill='#007AFF', font=_font(27))
    draw.text((1170, 664), f'FG {summary["foregroundFraction"]:.1%}', fill='#191B1E', font=_font(27))
    draw.text((98, 718), 'Foreground coverage is NOT segmentation accuracy. SAM inference is not claimed.', fill='#6C727A', font=_font(17))
    output.parent.mkdir(parents=True, exist_ok=True)
    board.save(output, format='PNG')
    output.with_suffix('.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('foreground', type=Path)
    parser.add_argument('painted', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(build_review(args.source, args.foreground, args.painted, args.report, args.output), sort_keys=True))


if __name__ == '__main__':
    main()
