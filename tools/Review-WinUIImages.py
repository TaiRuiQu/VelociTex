"""Create a contact sheet of actual HWND captures for human visual review."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

directory = Path(sys.argv[1] if len(sys.argv) > 1 else "artifacts/winui/aot")
names = [f"{theme}-{scale}.png" for theme in ("light", "dark") for scale in (100, 125, 150, 200)]
names += ["narrow-dark.png", "reloaded.png", "inline-ui-container-rejected.png"]
sheet = Image.new("RGB", (960, 440 * ((len(names) + 1) // 2)), "#dedede")
draw = ImageDraw.Draw(sheet)
for index, name in enumerate(names):
    with Image.open(directory / name) as source:
        image = source.convert("RGB")
    image.thumbnail((460, 404))
    x, y = (index % 2) * 480 + 10, (index // 2) * 440 + 28
    draw.text((x, y - 20), name, fill="black")
    sheet.paste(image, (x, y))
sheet.save(directory / "review-contact-sheet.png")
print(directory / "review-contact-sheet.png")
