"""Draws the synthetic album cover used by the UI preview (no real artwork).

Usage: python make_cover.py OUTPUT.jpg
"""
import math
import sys

from PIL import Image, ImageDraw

SIZE = 800


def main() -> None:
    img = Image.new("RGB", (SIZE, SIZE))
    draw = ImageDraw.Draw(img)
    for y in range(SIZE):
        t = y / SIZE
        draw.line([(0, y), (SIZE, y)], fill=(int(40 + 180 * t), int(60 + 40 * t), int(140 - 60 * t)))
    draw.ellipse((220, 140, 580, 500), fill=(255, 196, 120))
    for i in range(9):
        y = 520 + i * 26
        wave = [(x, y + 10 * math.sin(x / 60 + i)) for x in range(0, SIZE + 1, 20)]
        draw.line(wave, fill=(30, 40 + i * 12, 90 + i * 8), width=12)
    img.save(sys.argv[1], quality=90)


if __name__ == "__main__":
    main()
