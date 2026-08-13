#!/usr/bin/env python3
"""
Render the README screenshots.

Nothing here invents a result. `test/host_mockup_dump` runs the shipping
generator through the shipping decoder and the shipping grading engine and
prints what it found; this script only draws it. If the classifier changes its
mind about a page, the picture changes with it.

    make -C test mockup
    python3 tools_gen_mockups.py

Layout constants are copied from the view sources, and text is positioned by
*baseline* because that is what canvas_draw_str takes for its y coordinate.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 128, 64
SCALE = 4
BEZEL = 10

# Flipper's amber LCD, near enough.
LCD_ON = (26, 26, 24)
LCD_OFF = (255, 158, 45)
CASE = (58, 58, 62)

BLACK, WHITE = 0, 1

MENLO = "/System/Library/Fonts/Menlo.ttc"

# Menlo at 10px advances exactly 6px per character, which is the Flipper's
# 21-column grid. The secondary font on the device is proportional and averages
# closer to five, so it gets a smaller size here.
FONT_PRIMARY = ImageFont.truetype(MENLO, 10, index=1) if os.path.exists(MENLO) else None
FONT_SECONDARY = ImageFont.truetype(MENLO, 9, index=0) if os.path.exists(MENLO) else None

if FONT_PRIMARY is None:
    sys.exit(f"need {MENLO}")


class Screen:
    """A 128x64 one-bit canvas with the same primitives the C code uses."""

    def __init__(self):
        self.image = Image.new("1", (WIDTH, HEIGHT), WHITE)
        self.draw = ImageDraw.Draw(self.image)

    def line(self, x0, y0, x1, y1, color=BLACK):
        self.draw.line([x0, y0, x1, y1], fill=color)

    def box(self, x, y, w, h, color=BLACK):
        if w <= 0 or h <= 0:
            return
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)

    def frame(self, x, y, w, h, color=BLACK):
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], outline=color)

    def rframe(self, x, y, w, h, color=BLACK):
        self.draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=2, outline=color)

    def rbox(self, x, y, w, h, color=BLACK):
        self.draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=2, fill=color)

    def dot(self, x, y, color=BLACK):
        self.draw.point((x, y), fill=color)

    def text(self, x, baseline, string, font=None, color=BLACK):
        font = font or FONT_SECONDARY
        self.draw.text((x, baseline), string, font=font, fill=color, anchor="ls")

    def text_right(self, x, baseline, string, font=None, color=BLACK):
        font = font or FONT_SECONDARY
        self.draw.text((x, baseline), string, font=font, fill=color, anchor="rs")

    def text_center(self, x, baseline, string, font=None, color=BLACK):
        font = font or FONT_SECONDARY
        self.draw.text((x, baseline), string, font=font, fill=color, anchor="ms")

    def save(self, path):
        colour = Image.new("RGB", (WIDTH, HEIGHT))
        pixels = self.image.load()
        out = colour.load()
        for y in range(HEIGHT):
            for x in range(WIDTH):
                out[x, y] = LCD_ON if pixels[x, y] == BLACK else LCD_OFF

        colour = colour.resize((WIDTH * SCALE, HEIGHT * SCALE), Image.NEAREST)
        framed = Image.new(
            "RGB", (WIDTH * SCALE + BEZEL * 2, HEIGHT * SCALE + BEZEL * 2), CASE
        )
        framed.paste(colour, (BEZEL, BEZEL))
        framed.save(path)
        print(f"wrote {path}")
        return framed


# ------------------------------------------------------------------ shared --


def redact_bar(screen, x, y, width, text, mask, offset=0):
    """The signature visual: tall blocks where somebody's data is."""
    pitch = 3
    capacity = width // pitch
    for i in range(capacity):
        index = offset + i
        if index >= len(text):
            break
        if text[index] == " ":
            continue
        column = x + i * pitch
        if index < len(mask) and mask[index] == "#":
            screen.box(column, y, 2, 7)
        else:
            screen.box(column, y + 4, 2, 3)


def grade_badge(screen, x, y, grade):
    heavy = grade in ("D", "E", "F")
    width = max(15, 6 * len(grade) + 7)
    if heavy:
        screen.rbox(x, y, width, 15)
        screen.text_center(x + width // 2, y + 11, grade, FONT_PRIMARY, WHITE)
    else:
        screen.rframe(x, y, width, 15)
        screen.text_center(x + width // 2, y + 11, grade, FONT_PRIMARY)


def rssi_bars(screen, x, y, dbm):
    level = max(0, min(40, dbm + 100))
    bars = (level * 4) // 41
    for i in range(4):
        height = 2 + i * 2
        bx = x + i * 3
        by = y + 8 - height
        if i < bars:
            screen.box(bx, by, 2, height)
        else:
            screen.dot(bx, y + 7)


def frame_ruler(screen, x, y, mask, cursor):
    for i in range(8):
        bx = x + i * 5
        if mask & (1 << i):
            screen.box(bx, y, 4, 5)
        else:
            screen.frame(bx, y + 1, 4, 3)
    if cursor < 16:
        bx = x + (cursor >> 1) * 5
        screen.line(bx, y + 6, bx + 3, y + 6)


def leak_list(screen, x, baseline, width, leaks):
    text = leaks
    while text and screen.draw.textlength(text, font=FONT_SECONDARY) > width:
        text = text.rsplit(",", 1)[0]
    screen.text(x, baseline, text)


# ------------------------------------------------------------------ parser --


def parse(path):
    pages, pagers, status, tally = [], [], {}, {}
    current = None

    def kv(rest):
        out = {}
        for token in rest.split():
            if "=" in token:
                key, value = token.split("=", 1)
                out[key] = value
        return out

    for raw in open(path):
        line = raw.rstrip("\n")
        if line.startswith("STATUS "):
            status = kv(line[7:])
        elif line.startswith("PAGE "):
            current = kv(line[5:])
            current["text"] = ""
            current["mask"] = ""
            pages.append(current)
        elif line.startswith("TEXT "):
            current["text"] = line[5:]
        elif line == "TEXT":
            current["text"] = ""
        elif line.startswith("MASK "):
            current["mask"] = line[5:]
        elif line.startswith("FLOOR "):
            current["floor"] = line[6:]
        elif line.startswith("LEAKS "):
            current["leaks"] = line[6:]
        elif line.startswith("TALLY "):
            tally = kv(line[6:])
        elif line.startswith("PAGER "):
            pagers.append(kv(line[6:]))

    return pages, pagers, status, tally


def find_page(pages, ric, grade=None):
    for page in pages:
        if page["ric"] == str(ric) and (grade is None or page["grade"] == grade):
            return page
    return pages[0]


# ----------------------------------------------------------------- screens --


def screen_menu():
    screen = Screen()
    screen.text(2, 9, "Pheme", FONT_PRIMARY)
    screen.line(0, 12, 127, 12)

    items = [
        "Listen",
        "Demo channel",
        "Scan channels",
        "Pager log",
        "Save report",
    ]
    for i, item in enumerate(items):
        y = 15 + i * 10
        if i == 0:
            screen.box(0, y, 128, 10)
            screen.text(4, y + 8, item, FONT_PRIMARY, WHITE)
        else:
            screen.text(4, y + 8, item, FONT_PRIMARY)
    return screen


def screen_listen(page, status, tally):
    screen = Screen()

    screen.text(0, 8, "439.99", FONT_PRIMARY)
    screen.text(40, 8, "DAPNET ham")
    rssi_bars(screen, 116, 0, int(page["rssi"]))
    screen.line(0, 10, 127, 10)

    screen.text(0, 19, "LOCKED")
    screen.text(48, 19, "1200bd")
    frame_ruler(screen, 88, 13, 0b10010001, 6)

    screen.text(0, 31, page["ric"], FONT_PRIMARY)
    screen.text(58, 31, page["kind"])
    grade_badge(screen, 108, 22, page["grade"])

    redact_bar(screen, 0, 35, 104, page["text"], page["mask"])
    leak_list(screen, 0, 50, 126, page["leaks"])

    screen.line(0, 52, 127, 52)
    screen.text(0, 62, f"{tally['pages']} pages  {tally['capcodes']} pagers")
    screen.text_right(127, 62, "OK read")
    return screen


def message_header(screen, index, total, page):
    screen.text(0, 8, f"{index}/{total}  {page['ric']}", FONT_PRIMARY)
    screen.line(0, 10, 127, 10)
    grade_badge(screen, 110, 13, page["grade"])


def screen_message(page):
    screen = Screen()
    message_header(screen, 3, 16, page)

    capacity = 104 // 3
    y = 15
    for row in range(3):
        offset = row * capacity
        if offset >= len(page["text"]):
            break
        redact_bar(screen, 0, y, 104, page["text"], page["mask"], offset)
        y += 10

    screen.text(0, 51, f"{page['redacted']}/{page['chars']} chars personal")
    screen.line(0, 53, 127, 53)
    leak_list(screen, 0, 62, 80, page["leaks"])
    screen.text_right(127, 62, "OK hold")
    return screen


def screen_reveal(page):
    """The same page with the setting turned on and OK held down."""
    screen = Screen()
    message_header(screen, 3, 16, page)

    text, mask = page["text"], page["mask"]
    y = 20
    offset = 0
    while offset < len(text) and y < 62:
        take = len(text) - offset
        while take > 0 and screen.draw.textlength(text[offset : offset + take], FONT_SECONDARY) > 106:
            take -= 1
        cut = text.rfind(" ", offset, offset + take)
        if cut > offset and offset + take < len(text):
            take = cut - offset + 1

        line = text[offset : offset + take]
        screen.text(0, y, line)

        # Underline the runs the classifier claimed.
        run = None
        for i in range(take + 1):
            index = offset + i
            hot = i < take and index < len(mask) and mask[index] == "#"
            if hot and run is None:
                run = i
            elif not hot and run is not None:
                x0 = screen.draw.textlength(line[:run], FONT_SECONDARY)
                x1 = screen.draw.textlength(line[:i], FONT_SECONDARY)
                screen.line(int(x0), y + 2, int(x1), y + 2)
                run = None

        offset += take
        y += 10

    screen.line(0, 53, 127, 53)
    leak_list(screen, 0, 62, 80, page["leaks"])
    screen.text_right(127, 62, "OK hide")
    return screen


def screen_grade(page):
    screen = Screen()
    message_header(screen, 3, 16, page)

    screen.text(0, 20, f"Exposure {page['score']} of 100")
    screen.text(0, 31, "Capped by:")

    reason = page["floor"]
    y = 41
    offset = 0
    while offset < len(reason) and y < 62:
        take = len(reason) - offset
        while take > 0 and screen.draw.textlength(reason[offset : offset + take], FONT_SECONDARY) > 106:
            take -= 1
        cut = reason.rfind(" ", offset, offset + take)
        if cut > offset and offset + take < len(reason):
            take = cut - offset + 1
        screen.text(0, y, reason[offset : offset + take])
        offset += take
        y += 9

    screen.line(0, 53, 127, 53)
    screen.text(0, 62, "Grade")
    leak_list(screen, 44, 62, 83, page["leaks"])
    return screen


def screen_roster(pagers):
    screen = Screen()
    screen.text(0, 8, "Pager log", FONT_PRIMARY)
    screen.text_right(127, 8, f"{len(pagers)} capcodes")
    screen.line(0, 10, 127, 10)

    for row, pager in enumerate(pagers[:4]):
        y = 12 + row * 10
        selected = row == 0
        if selected:
            screen.box(0, y, 128, 10)
        colour = WHITE if selected else BLACK
        screen.text(2, y + 8, pager["ric"], color=colour)
        screen.text(48, y + 8, f"{pager['pages']}x", color=colour)
        screen.text(70, y + 8, pager["role"], color=colour)
        screen.text_right(126, y + 8, pager["grade"], color=colour)

    screen.line(0, 53, 127, 53)
    top = pagers[0]
    screen.text(
        0, 62, f"{top['named']} named {top['located']} located {top['minutes']}min"
    )
    return screen


def screen_scan():
    screen = Screen()
    screen.text(0, 8, "Scan", FONT_PRIMARY)
    screen.text_right(127, 8, "sweep 3")
    screen.line(0, 10, 127, 10)

    rows = [
        ("433.92", "On-site ISM", "quiet"),
        ("439.99", "DAPNET ham", "28 sync"),
        ("448.42", "Commercial", "quiet"),
        ("454.02", "US paging", "-"),
    ]
    for row, (label, use, right) in enumerate(rows):
        y = 12 + row * 10
        selected = row == 1
        if selected:
            screen.box(0, y, 128, 10)
        colour = WHITE if selected else BLACK
        screen.text(2, y + 8, label, color=colour)
        screen.text(40, y + 8, use, color=colour)
        if row == 3:
            screen.box(92, y + 3, 3, 3, color=colour)
        screen.text_right(126, y + 8, right, color=colour)

    screen.line(0, 53, 127, 53)
    screen.text(0, 62, "sync, not strength")
    screen.text_right(127, 62, "OK tune")
    return screen


def screen_explain():
    """Lesson 3 - the one the whole app is built around."""
    screen = Screen()
    screen.text(0, 8, "11 bits of armour", FONT_PRIMARY)
    screen.text_right(127, 8, "3/5")
    screen.line(0, 10, 127, 10)

    for i in range(32):
        x = 1 + i * 4
        if i >= 21:
            screen.box(x, 15, 3, 9)
        else:
            screen.frame(x, 15, 3, 9)

    for i in (6, 14):
        x = 1 + i * 4
        screen.box(x, 15, 3, 9)
        screen.line(x + 1, 11, x + 1, 13)

    screen.text(0, 33, "21 message")
    screen.text_right(127, 33, "11 armour")

    for i, line in enumerate(
        [
            "11 bits in 32 fight noise.",
            "None of them fight you.",
        ]
    ):
        screen.text(0, 47 + i * 9, line)
    return screen


def screen_coverage():
    screen = Screen()
    screen.text(0, 8, "What it can't hear", FONT_PRIMARY)
    screen.text_right(127, 8, "5/5")
    screen.line(0, 10, 127, 10)

    screen.text(0, 17, "what the CC1101 can tune")
    screen.line(2, 25, 125, 25)
    for low, high in ((300, 348), (387, 464), (779, 928)):
        x0 = 2 + ((low - 100) * 123) // 900
        x1 = 2 + ((high - 100) * 123) // 900
        screen.box(x0, 20, x1 - x0 + 1, 5)

    screen.text(0, 33, "100 MHz")
    screen.text_right(127, 33, "1 GHz")
    screen.text(0, 44, "929-932")
    screen.text(46, 44, "US national")
    screen.text(0, 52, "above the 928 MHz edge")
    screen.text(0, 62, "blank = deaf, not quiet")
    return screen


# -------------------------------------------------------------------- main --


def main():
    data = "test/mockup_data.txt"
    if not os.path.exists(data):
        sys.exit(f"{data} missing - run: make -C test mockup")

    pages, pagers, status, tally = parse(data)
    os.makedirs("images", exist_ok=True)

    worst = find_page(pages, 1234567, "F")
    newest = pages[0]

    shots = [
        ("images/screen_menu.png", screen_menu()),
        ("images/screen_listen.png", screen_listen(newest, status, tally)),
        ("images/screen_message.png", screen_message(worst)),
        ("images/screen_reveal.png", screen_reveal(worst)),
        ("images/screen_grade.png", screen_grade(worst)),
        ("images/screen_roster.png", screen_roster(pagers)),
        ("images/screen_scan.png", screen_scan()),
        ("images/screen_explain.png", screen_explain()),
        ("images/screen_coverage.png", screen_coverage()),
    ]

    rendered = [(path, shot.save(path)) for path, shot in shots]

    # A contact sheet, three across.
    tile_w, tile_h = rendered[0][1].size
    columns = 3
    rows = (len(rendered) + columns - 1) // columns
    sheet = Image.new("RGB", (tile_w * columns, tile_h * rows), CASE)
    for i, (_, image) in enumerate(rendered):
        sheet.paste(image, ((i % columns) * tile_w, (i // columns) * tile_h))
    sheet.save("images/screens.png")
    print("wrote images/screens.png")


if __name__ == "__main__":
    main()
