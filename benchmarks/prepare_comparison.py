"""Prepare equal-scale cell images for real Markdown comparison tables."""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="artifacts/comparison-preview6")
    parser.add_argument("--out", default="docs/benchmarks/rendering")
    args = parser.parse_args()
    data, output = Path(args.data), Path(args.out)
    output.mkdir(parents=True, exist_ok=True)
    for case in json.loads(Path("benchmarks/comparison.json").read_text(encoding="utf-8")):
        images = []
        for engine in ["native", "wasm", "katex"]:
            original = Image.open(data / f"{case['id']}-{engine}.png").convert("RGBA")
            white = Image.new("RGBA", original.size, "white")
            white.alpha_composite(original)
            image = white.convert("RGB")
            bounds = ImageChops.difference(image, Image.new("RGB", image.size, "white")).getbbox()
            if bounds is None:
                raise ValueError("Empty rendering")
            images.append((engine, image.crop(bounds)))
        # Identical canvases per row; never stretch each formula independently.
        size = (max(im.width for _, im in images) + 48, max(im.height for _, im in images) + 48)
        for engine, image in images:
            cell = Image.new("RGB", size, "white")
            cell.paste(image, ((size[0] - image.width) // 2, (size[1] - image.height) // 2))
            cell.save(output / f"{case['id']}-{engine}.png", optimize=True)
        print(case["id"], size)


if __name__ == "__main__":
    main()
