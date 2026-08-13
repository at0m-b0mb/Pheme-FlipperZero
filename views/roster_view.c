#include "roster_view.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

#define ROSTER_ROWS 4

struct RosterView {
    View* view;
};

typedef struct {
    PhmPager pager[PHM_ROSTER_MAX];
    uint8_t count;
    uint16_t overflow;
    uint8_t selected;
    uint8_t scroll;
} RosterModel;

static void roster_draw(Canvas* canvas, void* context) {
    const RosterModel* model = context;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 8, "Pager log");

    canvas_set_font(canvas, FontSecondary);
    char header[24];
    if(model->overflow) {
        snprintf(header, sizeof(header), "%u shown, %u more", model->count, model->overflow);
    } else {
        snprintf(header, sizeof(header), "%u capcodes", model->count);
    }
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, header);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    if(model->count == 0) {
        canvas_draw_str(canvas, 0, 26, "No pages heard yet.");
        canvas_draw_str(canvas, 0, 38, "Every page carries a");
        canvas_draw_str(canvas, 0, 47, "capcode, and it never changes.");
        return;
    }

    for(uint8_t row = 0; row < ROSTER_ROWS; row++) {
        uint8_t index = (uint8_t)(model->scroll + row);
        if(index >= model->count) break;

        const PhmPager* pager = &model->pager[index];
        uint8_t y = (uint8_t)(12 + row * 10);
        bool selected = (index == model->selected);

        if(selected) {
            canvas_draw_box(canvas, 0, y, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        char line[32];
        snprintf(line, sizeof(line), "%lu", (unsigned long)pager->ric);
        canvas_draw_str(canvas, 2, (uint8_t)(y + 8), line);

        snprintf(line, sizeof(line), "%ux", pager->pages > 999u ? 999u : pager->pages);
        canvas_draw_str(canvas, 48, (uint8_t)(y + 8), line);

        canvas_draw_str(canvas, 70, (uint8_t)(y + 8), phm_role_name(pager->role));

        canvas_draw_str_aligned(
            canvas,
            126,
            (uint8_t)(y + 8),
            AlignRight,
            AlignBottom,
            phm_grade_name(pager->worst_grade));

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_line(canvas, 0, 53, 127, 53);

    /*
     * The footer is where the point lands. A capcode plus a length of time is a
     * pattern of life, and it was assembled without touching the network, the
     * pager, or the person carrying it.
     */
    const PhmPager* pager = &model->pager[model->selected];
    uint32_t span_min = (pager->last_ms - pager->first_ms) / 60000u;
    char footer[44];

    if(pager->named || pager->located) {
        snprintf(
            footer,
            sizeof(footer),
            "%u named %u located %lumin",
            pager->named,
            pager->located,
            (unsigned long)(span_min > 999u ? 999u : span_min));
    } else {
        snprintf(
            footer,
            sizeof(footer),
            "%s, %lu min",
            phm_role_hint(pager->role),
            (unsigned long)(span_min > 999u ? 999u : span_min));
    }
    canvas_draw_str(canvas, 0, 62, footer);
}

static bool roster_input(InputEvent* event, void* context) {
    RosterView* view = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool handled = false;

    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        with_view_model(
            view->view,
            RosterModel * model,
            {
                if(model->count > 0) {
                    if(event->key == InputKeyUp) {
                        if(model->selected > 0) model->selected--;
                    } else {
                        if(model->selected + 1u < model->count) model->selected++;
                    }

                    /* Keep the cursor inside the window rather than paging it. */
                    if(model->selected < model->scroll) {
                        model->scroll = model->selected;
                    } else if(model->selected >= model->scroll + ROSTER_ROWS) {
                        model->scroll = (uint8_t)(model->selected - ROSTER_ROWS + 1u);
                    }
                }
            },
            true);
        handled = true;
    }

    return handled;
}

RosterView* roster_view_alloc(void) {
    RosterView* view = malloc(sizeof(RosterView));
    memset(view, 0, sizeof(RosterView));

    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(RosterModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, roster_draw);
    view_set_input_callback(view->view, roster_input);

    return view;
}

void roster_view_free(RosterView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* roster_view_get_view(RosterView* view) {
    furi_assert(view);
    return view->view;
}

void roster_view_set_data(
    RosterView* view,
    const PhmPager* pagers,
    uint8_t count,
    uint16_t overflow) {
    furi_assert(view);
    with_view_model(
        view->view,
        RosterModel * model,
        {
            if(count > PHM_ROSTER_MAX) count = PHM_ROSTER_MAX;
            model->count = count;
            model->overflow = overflow;
            for(uint8_t i = 0; i < count; i++) model->pager[i] = pagers[i];

            if(model->selected >= count) model->selected = count ? (uint8_t)(count - 1u) : 0u;
            if(model->scroll > model->selected) model->scroll = model->selected;
        },
        true);
}
