#include "phm_widgets.h"

#include <string.h>

uint8_t phm_redact_capacity(uint8_t width) {
    return (uint8_t)(width / PHM_REDACT_PITCH);
}

void phm_draw_redact_bar(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    const char* text,
    uint8_t len,
    uint8_t offset,
    const PhmExposure* exposure) {
    uint8_t capacity = phm_redact_capacity(width);

    for(uint8_t i = 0; i < capacity; i++) {
        uint8_t index = (uint8_t)(offset + i);
        if(index >= len) break;

        char c = text[index];
        if(c == ' ') continue; /* the gaps between words survive */

        uint8_t column = (uint8_t)(x + i * PHM_REDACT_PITCH);

        if(phm_privacy_is_sensitive(exposure, index)) {
            /* Full height, solid: this is a person's data. */
            canvas_draw_box(canvas, column, y, 2, 7);
        } else {
            /* A low block: there was a character here, and it was nobody's
             * business but the sender's. */
            canvas_draw_box(canvas, column, (uint8_t)(y + 4), 2, 3);
        }
    }
}

void phm_draw_grade_badge(Canvas* canvas, uint8_t x, uint8_t y, uint8_t grade) {
    const char* label = phm_grade_name(grade);
    bool heavy = grade >= PhmGradeD;

    canvas_set_font(canvas, FontPrimary);
    uint8_t width = (uint8_t)(canvas_string_width(canvas, label) + 7);
    if(width < 15) width = 15;

    if(heavy) {
        canvas_draw_rbox(canvas, x, y, width, 15, 2);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, width, 15, 2);
    }

    canvas_draw_str_aligned(
        canvas, (uint8_t)(x + width / 2), (uint8_t)(y + 8), AlignCenter, AlignCenter, label);

    if(heavy) canvas_set_color(canvas, ColorBlack);
}

void phm_draw_rssi(Canvas* canvas, uint8_t x, uint8_t y, int8_t dbm) {
    /* -100 dBm and below is nothing, -60 and above is a full house. */
    int16_t level = dbm + 100;
    if(level < 0) level = 0;
    if(level > 40) level = 40;
    uint8_t bars = (uint8_t)((level * 4) / 41);

    for(uint8_t i = 0; i < 4; i++) {
        uint8_t height = (uint8_t)(2 + i * 2);
        uint8_t bx = (uint8_t)(x + i * 3);
        uint8_t by = (uint8_t)(y + 8 - height);
        if(i < bars) {
            canvas_draw_box(canvas, bx, by, 2, height);
        } else {
            canvas_draw_dot(canvas, bx, (uint8_t)(y + 7));
        }
    }
}

void phm_draw_leak_list(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint16_t flags) {
    char line[48];
    const uint8_t capacity = (uint8_t)(sizeof(line) - 1u);
    uint8_t n = 0;
    line[0] = '\0';

    canvas_set_font(canvas, FontSecondary);

    for(uint8_t leak = 0; leak < PhmLeakCount; leak++) {
        if(!(flags & (1u << leak))) continue;

        const char* name = phm_leak_short(leak);
        uint8_t name_len = (uint8_t)strlen(name);
        uint8_t separator = n ? 2u : 0u;
        if((uint8_t)(n + separator + name_len) > capacity) break;

        if(separator) {
            line[n++] = ',';
            line[n++] = ' ';
        }
        memcpy(line + n, name, name_len);
        n = (uint8_t)(n + name_len);
        line[n] = '\0';

        /* Stop before the text runs off the edge rather than clipping a word
         * in half - a truncated leak name reads as a different leak. */
        if(canvas_string_width(canvas, line) > width) {
            n = (uint8_t)(n - separator - name_len);
            line[n] = '\0';
            break;
        }
    }

    if(n == 0) {
        canvas_draw_str(canvas, x, y, "nothing identifiable");
    } else {
        canvas_draw_str(canvas, x, y, line);
    }
}

void phm_draw_frame_ruler(Canvas* canvas, uint8_t x, uint8_t y, uint8_t mask, uint8_t cursor) {
    /*
     * The eight frames of a batch. A pager only listens to the one frame its
     * own capcode falls in - which is the whole of POCSAG's idea of privacy,
     * and it is a power-saving measure, not a privacy measure. Every other
     * pager could listen to all eight if it felt like it, and so can this.
     */
    for(uint8_t i = 0; i < 8; i++) {
        uint8_t bx = (uint8_t)(x + i * 5);
        if(mask & (1u << i)) {
            canvas_draw_box(canvas, bx, y, 4, 5);
        } else {
            canvas_draw_frame(canvas, bx, (uint8_t)(y + 1), 4, 3);
        }
    }

    if(cursor < 16) {
        uint8_t bx = (uint8_t)(x + (cursor >> 1) * 5);
        canvas_draw_line(canvas, bx, (uint8_t)(y + 6), (uint8_t)(bx + 3), (uint8_t)(y + 6));
    }
}
