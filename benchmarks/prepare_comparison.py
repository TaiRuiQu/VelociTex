"""Prepare equal-scale cell images for real Markdown comparison tables."""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="artifacts/comparison-html-css")
    parser.add_argument("--out", default="docs/benchmarks/rendering")
    args = parser.parse_args()
    data, output = Path(args.data), Path(args.out)
    output.mkdir(parents=True, exist_ok=True)
    rows = []
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
        cells = []
        for engine, image in images:
            cell = Image.new("RGB", size, "white")
            cell.paste(image, ((size[0] - image.width) // 2, (size[1] - image.height) // 2))
            cell.save(output / f"{case['id']}-{engine}.png", optimize=True)
            cells.append(cell)
        rows.append((case["id"], cells))
        print(case["id"], size)
    # A single overview for convenient review. One global scale preserves the
    # same mathematical em across every formula; full-size cells remain above.
    scale = min(1, 720 / max(cell.width for _, cells in rows for cell in cells))
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = ImageFont.truetype(str(font_path), 22) if font_path.exists() else ImageFont.load_default(size=22)
    row_heights = [round(max(cell.height for cell in cells) * scale) + 65 for _, cells in rows]
    preview = Image.new("RGB", (2240, 130 + sum(row_heights)), "white")
    draw = ImageDraw.Draw(preview)
    draw.text((24, 16), "VelociTex rendering preview | HTML/CSS WASM | 32 px math em, 2x capture", fill="#163245", font=font)
    for i, title in enumerate(["Native / Direct2D", "VelociTex HTML/CSS WASM", "KaTeX 0.19.0"]):
        draw.text((24 + i * 740, 65), title, fill="#163245", font=font)
    y = 115
    for (label, cells), height in zip(rows, row_heights):
        draw.text((24, y), label.capitalize(), fill="#586776", font=font)
        for i, cell in enumerate(cells):
            size = (max(1, round(cell.width * scale)), max(1, round(cell.height * scale)))
            thumb = cell.resize(size, Image.Resampling.LANCZOS)
            preview.paste(thumb, (24 + i * 740 + (720 - thumb.width) // 2, y + 36))
        y += height
        draw.line((24, y - 5, 2216, y - 5), fill="#e3e9ed", width=1)
    preview.save(output / "comparison-preview.png", optimize=True)


if __name__ == "__main__":
    main()
