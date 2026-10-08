"""Convert the selected transparent merchant mark to native icon sizes.

Creates only new merchant assets; never replaces the student brand files.
"""
from pathlib import Path
import argparse
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    sizes = (16, 24, 32, 48, 64, 128, 256)
    outputs = [args.output_dir / f"brand-{size}.png" for size in sizes]
    ico = args.output_dir / "paperroom-merchant.ico"
    if any(path.exists() for path in (*outputs, ico)):
        parser.error("Merchant icon outputs already exist; choose a new directory")
    with Image.open(args.source) as image:
        if image.width != image.height or "A" not in image.getbands():
            parser.error("Expected a square transparent image")
        mark = image.convert("RGBA")
        if mark.getchannel("A").getextrema()[0] != 0:
            parser.error("Expected genuinely transparent margins")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        for size, path in zip(sizes, outputs):
            mark.resize((size, size), Image.Resampling.LANCZOS).save(path)
        mark.save(ico, format="ICO", sizes=[(size, size) for size in sizes])
    print(f"Added {len(outputs)} PNG sizes and {ico}")


if __name__ == "__main__":
    main()
