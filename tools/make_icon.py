"""Draws KaiPlayer's icon (icon.jpg, 256x256): a flat pair of beamed notes.

Usage: python tools/make_icon.py [OUT]
"""
import sys

from icon_render import Canvas, ellipse, polygon, round_rect, save_icon, union

PINK = (255, 107, 154)


def draw() -> Canvas:
    canvas = Canvas()
    stems = union(round_rect(88, 62, 104, 176, 3), round_rect(184, 44, 200, 158, 3))
    beam = polygon([(92, 66), (196, 46), (196, 68), (92, 88)]) - 4.0
    heads = union(ellipse(72, 178, 33, 25, -20), ellipse(168, 160, 33, 25, -20))
    note = union(stems, beam, heads)
    canvas.long_shadow(note)
    canvas.fill(note, PINK)
    return canvas


def main() -> None:
    save_icon(draw().result(), sys.argv[1] if len(sys.argv) > 1 else "icon.jpg")


if __name__ == "__main__":
    main()
