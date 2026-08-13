#!/usr/bin/env python3
"""
Repo branding: the README banner and the GitHub social preview.

The motif is the redaction skyline the app itself draws - a row of blocks, tall
wherever a person's data sits - because that is the one image that explains what
Pheme is before you have read a word about it.

    python3 tools_gen_banner.py
"""

import os

from PIL import Image, ImageDraw, ImageFont

# Amber on near-black: the Flipper's own palette, turned up.
INK = (14, 14, 16)
AMBER = (255, 158, 45)
AMBER_DIM = (150, 92, 26)
PAPER = (236, 232, 226)

FONTS = "/System/Library/Fonts/"
SUPPLEMENTAL = FONTS + "Supplemental/"


def font(size, bold=False):
    for path, index in (
        (FONTS + "Menlo.ttc", 1 if bold else 0),
        (SUPPLEMENTAL + "Andale Mono.ttf", 0),
        (FONTS + "Monaco.ttf", 0),
    ):
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size, index=index)
            except OSError:
                return ImageFont.truetype(path, size)
    return ImageFont.load_default()


# The demo channel's worst page, and the mask the classifier produced for it.
# Copied from test/mockup_data.txt so the banner shows a real classification.
SAMPLE = "PT J DOE NHS 4990000000 TRANSFER TO ITU BED 3"
MASK = "## # ### ### ########## ........ .. ... ### #"


def skyline(draw, x, y, pitch, tall, short, width_limit=None):
    """The app's redaction bar, scaled up."""
    for i, char in enumerate(SAMPLE):
        column = x + i * pitch
        if width_limit and column > width_limit:
            break
        if char == " ":
            continue
        hot = i < len(MASK) and MASK[i] == "#"
        if hot:
            draw.rectangle([column, y, column + pitch - 3, y + tall], fill=AMBER)
        else:
            draw.rectangle(
                [column, y + tall - short, column + pitch - 3, y + tall], fill=AMBER_DIM
            )


def banner(path, width=1280, height=440):
    image = Image.new("RGB", (width, height), INK)
    draw = ImageDraw.Draw(image)

    title = font(96, bold=True)
    tagline = font(30)
    small = font(22)

    draw.text((70, 78), "PHEME", font=title, fill=AMBER)
    draw.text(
        (74, 190),
        "everyone hears the page",
        font=tagline,
        fill=PAPER,
    )
    draw.text(
        (74, 236),
        "POCSAG pager privacy educator for the Flipper Zero",
        font=small,
        fill=AMBER_DIM,
    )

    # The grading scale, right-hand side. A+ is drawn struck through because it
    # is not a grade Pheme can award - which is the argument in one glyph.
    scale = [
        ("A+", "unreachable: it is cleartext", False),
        ("B", "the best any page can do", True),
        ("F", "a name, and where they are", True),
    ]
    badge = font(34, bold=True)
    for row, (grade, note, solid) in enumerate(scale):
        y = 96 + row * 62
        box = [744, y, 804, y + 46]
        if solid:
            draw.rounded_rectangle(box, radius=8, fill=AMBER)
            draw.text((774, y + 23), grade, font=badge, fill=INK, anchor="mm")
        else:
            draw.rounded_rectangle(box, radius=8, outline=AMBER_DIM, width=3)
            draw.text((774, y + 23), grade, font=badge, fill=AMBER_DIM, anchor="mm")
            draw.line([750, y + 23, 798, y + 23], fill=AMBER_DIM, width=3)
        draw.text((824, y + 23), note, font=small, fill=PAPER if solid else AMBER_DIM, anchor="lm")

    # The skyline, with a caption that explains what the tall blocks are.
    skyline(draw, 74, 316, 24, 62, 22, width_limit=1180)
    draw.text(
        (74, 396),
        "tall blocks are somebody's name, ward, record number",
        font=small,
        fill=AMBER_DIM,
    )

    image.save(path)
    print(f"wrote {path}")


def social(path, width=1280, height=640):
    """GitHub's social preview card. Must read at thumbnail size."""
    image = Image.new("RGB", (width, height), INK)
    draw = ImageDraw.Draw(image)

    draw.text((80, 120), "PHEME", font=font(120, bold=True), fill=AMBER)
    draw.text((86, 262), "everyone hears the page", font=font(38), fill=PAPER)

    skyline(draw, 86, 350, 26, 70, 24, width_limit=width - 90)

    draw.text(
        (86, 470),
        "POCSAG has no encryption and no authentication.",
        font=font(28),
        fill=AMBER_DIM,
    )
    draw.text(
        (86, 512),
        "Hospitals still page in the clear. This shows you how much that leaks.",
        font=font(28),
        fill=AMBER_DIM,
    )
    draw.text((86, 566), "github.com/at0m-b0mb/Pheme-FlipperZero", font=font(24), fill=AMBER_DIM)

    image.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    os.makedirs("images", exist_ok=True)
    banner("images/banner.png")
    social("images/social-preview.png")
