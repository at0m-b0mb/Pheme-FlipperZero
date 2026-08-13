#include "explain_view.h"

#include "../helpers/phm_pocsag.h"
#include "../helpers/phm_radio.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

struct ExplainView {
    View* view;
};

typedef struct {
    uint8_t lesson;
    uint32_t frame;
} ExplainModel;

/* ------------------------------------------------------------ primitives -- */

static void draw_mast(Canvas* canvas, uint8_t x, uint8_t y) {
    canvas_draw_line(canvas, x, y, x, (uint8_t)(y + 12));
    canvas_draw_line(canvas, (uint8_t)(x - 4), (uint8_t)(y + 12), (uint8_t)(x + 4), (uint8_t)(y + 12));
    canvas_draw_line(canvas, x, y, (uint8_t)(x - 3), (uint8_t)(y + 4));
    canvas_draw_line(canvas, x, y, (uint8_t)(x + 3), (uint8_t)(y + 4));
}

/* Expanding arcs, built from short lines because canvas_draw_arc does not
 * exist in the firmware API. */
static void draw_wave(Canvas* canvas, uint8_t x, uint8_t y, uint8_t radius) {
    if(radius == 0) return;
    canvas_draw_line(
        canvas,
        (uint8_t)(x + radius),
        (uint8_t)(y - 3),
        (uint8_t)(x + radius + 2),
        (uint8_t)(y));
    canvas_draw_line(
        canvas,
        (uint8_t)(x + radius + 2),
        (uint8_t)(y),
        (uint8_t)(x + radius),
        (uint8_t)(y + 3));
}

static void draw_pager(Canvas* canvas, uint8_t x, uint8_t y, bool lit) {
    if(lit) {
        canvas_draw_rbox(canvas, x, y, 11, 14, 2);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, (uint8_t)(x + 2), (uint8_t)(y + 3), 7, 4);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_rframe(canvas, x, y, 11, 14, 2);
        canvas_draw_line(
            canvas, (uint8_t)(x + 2), (uint8_t)(y + 4), (uint8_t)(x + 8), (uint8_t)(y + 4));
        canvas_draw_line(
            canvas, (uint8_t)(x + 2), (uint8_t)(y + 6), (uint8_t)(x + 8), (uint8_t)(y + 6));
    }
}

static void draw_lines(Canvas* canvas, uint8_t y, const char* const* lines, uint8_t count) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < count; i++) {
        canvas_draw_str(canvas, 0, (uint8_t)(y + i * 9), lines[i]);
    }
}

/* ---------------------------------------------------------------- lesson -- */

/*
 * 1. Addressing is not privacy.
 *
 * Every pager on the channel receives every bit of every batch. The capcode
 * decides which one beeps. Nothing about it decides which one can read.
 */
static void lesson_broadcast(Canvas* canvas, const ExplainModel* model) {
    uint8_t phase = (uint8_t)(model->frame % 24u);

    draw_mast(canvas, 10, 14);
    for(uint8_t i = 0; i < 3; i++) {
        uint8_t radius = (uint8_t)((phase + i * 8u) % 24u);
        draw_wave(canvas, 10, 20, radius);
    }

    /* The addressed pager beeps. The other three heard exactly the same bits. */
    for(uint8_t i = 0; i < 4; i++) {
        draw_pager(canvas, (uint8_t)(52 + i * 18), 16, i == 1 && phase > 12);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 50, 43, "one beeps. all four");

    static const char* const lines[] = {
        "A capcode picks who wakes up,",
        "not who is able to read.",
    };
    draw_lines(canvas, 53, lines, 2);
}

/*
 * 2. The shape of a batch.
 */
static void lesson_batch(Canvas* canvas, const ExplainModel* model) {
    uint8_t cursor = (uint8_t)(model->frame % 30u);

    canvas_set_font(canvas, FontSecondary);

    /* Preamble: the literal 1010 pattern. */
    for(uint8_t i = 0; i < 16; i += 2) {
        canvas_draw_dot(canvas, (uint8_t)(2 + i), (uint8_t)(16 + ((i / 2u) & 1u ? 0 : 4)));
    }
    canvas_draw_str(canvas, 0, 32, "1010");

    /* Sync codeword. */
    canvas_draw_box(canvas, 20, 14, 12, 8);
    canvas_draw_str(canvas, 20, 32, "sync");

    /* Eight frames of two codewords each. */
    for(uint8_t i = 0; i < 8; i++) {
        uint8_t x = (uint8_t)(36 + i * 11);
        canvas_draw_frame(canvas, x, 14, 5, 8);
        canvas_draw_frame(canvas, (uint8_t)(x + 5), 14, 5, 8);
        if(i == 3) {
            canvas_draw_box(canvas, x, 14, 5, 8);
            canvas_draw_str(canvas, (uint8_t)(x - 4), 32, "yours");
        }
    }

    /* A marker walking the batch, so the structure is seen in motion. */
    uint8_t marker = (cursor < 2) ? (uint8_t)(2 + cursor * 9)
                                  : (uint8_t)(36 + ((cursor - 2u) * 11u) / 3u);
    if(marker < 124) canvas_draw_line(canvas, marker, 24, (uint8_t)(marker + 2), 24);

    static const char* const lines[] = {
        "Wake-up, sync, 8 frames.",
        "Your pager sleeps through 7,",
        "to save power - not you.",
    };
    draw_lines(canvas, 43, lines, 3);
}

/*
 * 3. The punchline.
 */
static void lesson_bch(Canvas* canvas, const ExplainModel* model) {
    uint8_t phase = (uint8_t)((model->frame / 6u) % 4u);

    /* One 32-bit codeword: 21 bits of message, 10 of BCH, 1 of parity. */
    for(uint8_t i = 0; i < 32; i++) {
        uint8_t x = (uint8_t)(1 + i * 4);
        bool armour = (i >= 21);
        if(armour) {
            canvas_draw_box(canvas, x, 15, 3, 9);
        } else {
            canvas_draw_frame(canvas, x, 15, 3, 9);
        }
    }

    /* Two bits take a hit, and are put back. */
    if(phase == 1 || phase == 2) {
        for(uint8_t k = 0; k < 2; k++) {
            uint8_t i = (k == 0) ? 6u : 14u;
            uint8_t x = (uint8_t)(1 + i * 4);
            canvas_draw_box(canvas, x, 15, 3, 9);
            canvas_draw_line(canvas, (uint8_t)(x + 1), 11, (uint8_t)(x + 1), 13);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 33, "21 message");
    canvas_draw_str_aligned(canvas, 127, 33, AlignRight, AlignBottom, "11 armour");

    if(phase == 3) {
        canvas_draw_str(canvas, 52, 33, "repaired");
    }

    static const char* const lines[] = {
        "11 bits in 32 fight noise.",
        "None of them fight you.",
    };
    draw_lines(canvas, 47, lines, 2);
}

/*
 * 4. Why nobody ever fixed it.
 */
static void lesson_oneway(Canvas* canvas, const ExplainModel* model) {
    uint8_t phase = (uint8_t)(model->frame % 20u);

    draw_mast(canvas, 12, 14);
    draw_pager(canvas, 104, 16, false);

    /* A packet crossing left to right, and a return path that does not exist. */
    uint8_t x = (uint8_t)(24 + (phase * 3u) % 76u);
    canvas_draw_box(canvas, x, 19, 4, 3);
    canvas_draw_line(canvas, 24, 26, 100, 26);
    canvas_draw_line(canvas, 96, 23, 100, 26);
    canvas_draw_line(canvas, 96, 29, 100, 26);

    canvas_draw_line(canvas, 24, 34, 100, 34);
    canvas_draw_line(canvas, 55, 30, 69, 38);
    canvas_draw_line(canvas, 55, 38, 69, 30);

    static const char* const lines[] = {
        "A pager cannot answer, so it",
        "cannot agree a key. The kit",
        "is 1980s and still running.",
    };
    draw_lines(canvas, 43, lines, 3);
}

/*
 * 5. What this radio cannot hear. An empty screen has two explanations and the
 *    app should never let the user assume the flattering one.
 */
static void lesson_coverage(Canvas* canvas, const ExplainModel* model) {
    uint8_t highlight = (uint8_t)((model->frame / 12u) % PHM_BLIND_COUNT);

    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str(canvas, 0, 17, "what the CC1101 can tune");

    /* A ruler from 100 to 1000 MHz with the CC1101's three windows shaded. */
    canvas_draw_line(canvas, 2, 25, 125, 25);
    struct {
        uint16_t low;
        uint16_t high;
    } windows[] = {{300, 348}, {387, 464}, {779, 928}};

    for(uint8_t i = 0; i < 3; i++) {
        uint8_t x0 = (uint8_t)(2 + ((windows[i].low - 100u) * 123u) / 900u);
        uint8_t x1 = (uint8_t)(2 + ((windows[i].high - 100u) * 123u) / 900u);
        canvas_draw_box(canvas, x0, 20, (uint8_t)(x1 - x0 + 1u), 5);
    }

    canvas_draw_str(canvas, 0, 33, "100 MHz");
    canvas_draw_str_aligned(canvas, 127, 33, AlignRight, AlignBottom, "1 GHz");

    /* Label and use are drawn as two columns rather than one joined string:
     * concatenating them overran the screen for the longer allocations. */
    const PhmBlindSpot* spot = &phm_blind_spots[highlight];
    canvas_draw_str(canvas, 0, 44, spot->label);
    canvas_draw_str(canvas, 46, 44, spot->use);
    canvas_draw_str(canvas, 0, 52, spot->why);

    canvas_draw_str(canvas, 0, 62, "blank = deaf, not quiet");
}

/* ------------------------------------------------------------------ draw -- */

/* Seventeen characters is what fits beside the "n/5" counter in the primary
 * font. The titles were written to that limit rather than trimmed to it. */
static const char* const lesson_title[PHM_LESSON_COUNT] = {
    "Everyone hears it",
    "Inside a batch",
    "11 bits of armour",
    "Why it's in clear",
    "What it can't hear",
};

static void explain_draw(Canvas* canvas, void* context) {
    const ExplainModel* model = context;
    uint8_t lesson = model->lesson % PHM_LESSON_COUNT;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 8, lesson_title[lesson]);

    canvas_set_font(canvas, FontSecondary);
    char counter[10];
    snprintf(counter, sizeof(counter), "%u/%u", (uint8_t)(lesson + 1u), PHM_LESSON_COUNT);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, counter);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    switch(lesson) {
    case 0:
        lesson_broadcast(canvas, model);
        break;
    case 1:
        lesson_batch(canvas, model);
        break;
    case 2:
        lesson_bch(canvas, model);
        break;
    case 3:
        lesson_oneway(canvas, model);
        break;
    default:
        lesson_coverage(canvas, model);
        break;
    }
}

static bool explain_input(InputEvent* event, void* context) {
    ExplainView* view = context;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        with_view_model(
            view->view,
            ExplainModel * model,
            {
                if(event->key == InputKeyRight) {
                    model->lesson = (uint8_t)((model->lesson + 1u) % PHM_LESSON_COUNT);
                } else {
                    model->lesson =
                        (uint8_t)((model->lesson + PHM_LESSON_COUNT - 1u) % PHM_LESSON_COUNT);
                }
                model->frame = 0;
            },
            true);
        return true;
    }

    return false;
}

ExplainView* explain_view_alloc(void) {
    ExplainView* view = malloc(sizeof(ExplainView));
    memset(view, 0, sizeof(ExplainView));

    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(ExplainModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, explain_draw);
    view_set_input_callback(view->view, explain_input);

    return view;
}

void explain_view_free(ExplainView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* explain_view_get_view(ExplainView* view) {
    furi_assert(view);
    return view->view;
}

void explain_view_set_lesson(ExplainView* view, uint8_t lesson) {
    furi_assert(view);
    with_view_model(
        view->view,
        ExplainModel * model,
        {
            model->lesson = (uint8_t)(lesson % PHM_LESSON_COUNT);
            model->frame = 0;
        },
        true);
}

void explain_view_tick(ExplainView* view) {
    furi_assert(view);
    with_view_model(view->view, ExplainModel * model, { model->frame++; }, true);
}
