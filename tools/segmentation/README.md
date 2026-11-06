# Guided photo segmentation

Use your own opaque photograph and a same-size grayscale seed PNG. Coordinates correspond to the EXIF-oriented image. Fill unknown regions with 85 or 170; paint at least five certain foreground and background pixels. Labels: 0 = certain background, 85 = probable background, 170 = probable foreground, 255 = certain foreground. Do not antialias the seed map.

```sh
sudo apt-get install python3-opencv python3-numpy python3-pil
python3 tools/segmentation/guided_grabcut.py --image photo.jpg --seeds seeds.png --output masks-output --name Subject
./DrawboradHeadless run --manifest "$PWD/masks-output/manifest.json" --output painting-output --accept-all
```

Both output directories must be new. Outputs include preserved oriented RGB source, seeds, two independent RGBA layers, complementary binary masks, provenance and native-compatible manifest. Review the foreground before accepting it. The two layers exactly partition visible source pixels; obscured surfaces are not filled. This is assisted foreground/background extraction, not automatic multi-object semantic segmentation or SAM. Maximum native dimensions: 4096 per side and 4 megapixels total.
