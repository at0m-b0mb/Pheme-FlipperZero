#include "listen_view.h"

#include "phm_widgets.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

struct ListenView {
    View* view;
    ListenCallback open_callback;
    void* context;
};

typedef struct {
    ListenData data;
    uint8_t anim;
} ListenModel;

static const char* listen_lock_text(uint8_t lock) {
    switch(lock) {
    case PhmLockPreamble:
        return "PREAMBLE";
    case PhmLockSync:
        return "SYNC";
    case PhmLockBatch:
        return "LOCKED";
    default:
        return "listening";
    }
}

/*
 * The waiting animation is the preamble itself: 1010101010, sliding left. It is
 * the literal bit pattern a base station sends to wake pagers up, so a user who
 * later reads the explainer recognises the screen they spent five minutes
 * watching.
 */
static void listen_draw_carrier(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint8_t phase) {
    for(uint8_t i = 0; i < width; i += 2) {
        uint8_t bit = (uint8_t)(((i / 2u) + phase) & 1u);
        canvas_draw_dot(canvas, (uint8_t)(x + i), (uint8_t)(y + (bit ? 0 : 3)));
    }
}

static void listen_draw(Canvas* canvas, void* context) {
    const ListenModel* model = context;
    const ListenData* data = &model->data;

    canvas_clear(canvas);

    /* ---- header: where we are listening ---------------------------------- */
    canvas_set_font(canvas, FontPrimary);
    if(data->demo) {
        canvas_draw_box(canvas, 0, 0, 33, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 3, 8, "DEMO");
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 0, 8, data->channel);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 40, 8, data->demo ? "simulated channel" : data->use);

    if(!data->demo) phm_draw_rssi(canvas, 116, 0, data->rssi);

    canvas_draw_line(canvas, 0, 10, 127, 10);

    /* ---- status: what the receiver is doing right now --------------------- */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 19, listen_lock_text(data->lock));

    if(data->baud_idx >= 0 && data->baud_idx < (int8_t)PHM_BAUD_COUNT) {
        char baud[12];
        snprintf(baud, sizeof(baud), "%sbd", phm_bauds[data->baud_idx].label);
        canvas_draw_str(canvas, 48, 19, baud);
    } else {
        listen_draw_carrier(canvas, 48, 14, 28, model->anim);
    }

    phm_draw_frame_ruler(canvas, 88, 13, data->frame_mask, data->word_idx);

    /* ---- the last page ---------------------------------------------------- */
    if(data->have_page) {
        char ric[20];
        snprintf(ric, sizeof(ric), "%lu", (unsigned long)data->ric);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 31, ric);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 58, 31, phm_page_kind_name(data->kind));

        phm_draw_grade_badge(canvas, 108, 22, data->exposure.grade);

        if(data->len) {
            phm_draw_redact_bar(canvas, 0, 35, 104, data->text, data->len, 0, &data->exposure);
        } else {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 0, 41, "beeped, said nothing");
        }

        canvas_set_font(canvas, FontSecondary);
        phm_draw_leak_list(canvas, 0, 50, 126, data->exposure.flags);
    } else {
        canvas_set_font(canvas, FontSecondary);
        if(data->lock >= PhmLockSync) {
            canvas_draw_str(canvas, 0, 31, "POCSAG framing found.");
            canvas_draw_str(canvas, 0, 41, "Waiting for a page.");
        } else {
            canvas_draw_str(canvas, 0, 31, "Nothing here yet.");
            canvas_draw_str(canvas, 0, 41, "Paging is bursty. Give it");
            canvas_draw_str(canvas, 0, 49, "a few minutes.");
        }
    }

    /* ---- footer: the session so far --------------------------------------- */
    canvas_draw_line(canvas, 0, 52, 127, 52);

    char footer[36];
    if(data->bad_words) {
        snprintf(
            footer,
            sizeof(footer),
            "%lu pg %u pgr %lu lost",
            (unsigned long)(data->pages > 999u ? 999u : data->pages),
            data->capcodes > 99u ? 99u : data->capcodes,
            (unsigned long)(data->bad_words > 999u ? 999u : data->bad_words));
    } else {
        snprintf(
            footer,
            sizeof(footer),
            "%lu pages  %u pagers",
            (unsigned long)(data->pages > 999u ? 999u : data->pages),
            data->capcodes > 99u ? 99u : data->capcodes);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 62, footer);

    if(data->have_page) {
        canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, "OK read");
    }
}

static bool listen_input(InputEvent* event, void* context) {
    ListenView* view = context;

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        bool have = false;
        with_view_model(
            view->view, ListenModel * model, { have = model->data.have_page; }, false);
        if(have && view->open_callback) {
            view->open_callback(view->context);
            return true;
        }
    }

    return false;
}

ListenView* listen_view_alloc(void) {
    ListenView* view = malloc(sizeof(ListenView));
    memset(view, 0, sizeof(ListenView));

    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(ListenModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, listen_draw);
    view_set_input_callback(view->view, listen_input);

    return view;
}

void listen_view_free(ListenView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* listen_view_get_view(ListenView* view) {
    furi_assert(view);
    return view->view;
}

void listen_view_set_data(ListenView* view, const ListenData* data) {
    furi_assert(view);
    with_view_model(view->view, ListenModel * model, { model->data = *data; }, true);
}

void listen_view_tick(ListenView* view) {
    furi_assert(view);
    with_view_model(view->view, ListenModel * model, { model->anim++; }, true);
}

void listen_view_set_open_callback(ListenView* view, ListenCallback callback, void* context) {
    furi_assert(view);
    view->open_callback = callback;
    view->context = context;
}
