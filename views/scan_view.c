#include "scan_view.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

#define SCAN_ROWS 4

struct ScanView {
    View* view;
    ScanPickCallback pick_callback;
    void* context;
};

typedef struct {
    PhmScanBand band[PHM_CHANNEL_COUNT];
    uint8_t count;
    uint32_t sweeps;
    uint8_t current; /* the channel being dwelled on right now */
    uint8_t selected;
    uint8_t scroll;
} ScanModel;

static void scan_draw(Canvas* canvas, void* context) {
    const ScanModel* model = context;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 8, "Scan");

    canvas_set_font(canvas, FontSecondary);
    char header[24];
    snprintf(header, sizeof(header), "sweep %lu", (unsigned long)model->sweeps);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, header);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    for(uint8_t row = 0; row < SCAN_ROWS; row++) {
        uint8_t index = (uint8_t)(model->scroll + row);
        if(index >= model->count) break;

        const PhmScanBand* band = &model->band[index];
        uint8_t y = (uint8_t)(12 + row * 10);
        bool selected = (index == model->selected);

        if(selected) {
            canvas_draw_box(canvas, 0, y, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_draw_str(canvas, 2, (uint8_t)(y + 8), phm_channels[index].label);
        canvas_draw_str(canvas, 40, (uint8_t)(y + 8), phm_channels[index].use);

        /* A dot beside the channel currently being listened to, so the sweep is
         * visibly moving even on a completely dead band. */
        if(index == model->current) canvas_draw_box(canvas, 92, (uint8_t)(y + 3), 3, 3);

        char right[16];
        if(!band->seen) {
            snprintf(right, sizeof(right), "-");
        } else if(band->batches == 0) {
            snprintf(right, sizeof(right), "quiet");
        } else {
            snprintf(
                right,
                sizeof(right),
                "%u sync",
                band->batches > 999u ? 999u : band->batches);
        }
        canvas_draw_str_aligned(canvas, 126, (uint8_t)(y + 8), AlignRight, AlignBottom, right);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_line(canvas, 0, 53, 127, 53);
    canvas_set_font(canvas, FontSecondary);

    /*
     * "Quiet" is a real answer and is worded as one. The scan counts POCSAG
     * batches, not signal strength, so a channel with a loud carrier on it and
     * no paging still reads quiet - which is the whole reason to scan this way.
     */
    canvas_draw_str(canvas, 0, 62, "sync, not strength");
    canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, "OK tune");
}

static bool scan_input(InputEvent* event, void* context) {
    ScanView* view = context;

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        uint8_t selected = 0;
        with_view_model(view->view, ScanModel * model, { selected = model->selected; }, false);
        if(view->pick_callback) view->pick_callback(view->context, selected);
        return true;
    }

    if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
       (event->key == InputKeyUp || event->key == InputKeyDown)) {
        with_view_model(
            view->view,
            ScanModel * model,
            {
                if(model->count > 0) {
                    if(event->key == InputKeyUp) {
                        if(model->selected > 0) model->selected--;
                    } else {
                        if(model->selected + 1u < model->count) model->selected++;
                    }

                    if(model->selected < model->scroll) {
                        model->scroll = model->selected;
                    } else if(model->selected >= model->scroll + SCAN_ROWS) {
                        model->scroll = (uint8_t)(model->selected - SCAN_ROWS + 1u);
                    }
                }
            },
            true);
        return true;
    }

    return false;
}

ScanView* scan_view_alloc(void) {
    ScanView* view = malloc(sizeof(ScanView));
    memset(view, 0, sizeof(ScanView));

    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(ScanModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, scan_draw);
    view_set_input_callback(view->view, scan_input);

    return view;
}

void scan_view_free(ScanView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* scan_view_get_view(ScanView* view) {
    furi_assert(view);
    return view->view;
}

void scan_view_set_data(
    ScanView* view,
    const PhmScanBand* bands,
    uint8_t count,
    uint32_t sweeps,
    uint8_t current) {
    furi_assert(view);
    with_view_model(
        view->view,
        ScanModel * model,
        {
            if(count > PHM_CHANNEL_COUNT) count = PHM_CHANNEL_COUNT;
            model->count = count;
            model->sweeps = sweeps;
            model->current = current;
            for(uint8_t i = 0; i < count; i++) model->band[i] = bands[i];
            if(model->selected >= count) model->selected = count ? (uint8_t)(count - 1u) : 0u;
        },
        true);
}

void scan_view_set_pick_callback(ScanView* view, ScanPickCallback callback, void* context) {
    furi_assert(view);
    view->pick_callback = callback;
    view->context = context;
}
