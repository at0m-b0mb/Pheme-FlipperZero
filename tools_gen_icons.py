#!/usr/bin/env python3
"""
Draw the icons the firmware build compiles into the .fap.

Everything Pheme draws on screen is drawn in C - these are the two things fbt
needs as PNG files. Run from the repo root:

    python3 tools_gen_icons.py
"""

from PIL import Image, ImageDraw

BLACK = 0
WHITE = 1


def new(width, height):
    image = Image.new("1", (width, height), WHITE)
    return image, ImageDraw.Draw(image)


def app_icon(path):
    """
    10x10: a pager, seen from the front, with a page arriving.

    At this size a pager reads as a rounded box with a screen; the two dots off
    the top-left corner are the transmission, which is the half of the story
    the app is actually about.
    """
    image, draw = new(10, 10)

    # body
    draw.rectangle([3, 1, 9, 9], outline=BLACK, fill=WHITE)
    # screen
    draw.rectangle([4, 2, 8, 4], fill=BLACK)
    # two buttons
    draw.point((5, 6), fill=BLACK)
    draw.point((7, 6), fill=BLACK)

    # the page, arriving from off-screen
    draw.point((0, 1), fill=BLACK)
    draw.point((1, 2), fill=BLACK)
    draw.point((0, 4), fill=BLACK)
    draw.point((1, 5), fill=BLACK)

    image.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    app_icon("icons/pheme_10px.png")
