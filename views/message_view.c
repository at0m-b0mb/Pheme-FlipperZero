#include "message_view.h"

#include "phm_widgets.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

#define MSG_PANEL_COUNT 3

typedef enum {
    MsgPanelRedacted,
    MsgPanelGrade,
    MsgPanelSignal,
} MsgPanel;

struct MessageView {
    View* view;
    MessageStepCallback step_callback;
    void* context;
};

typedef struct {
    PhmRecord record;
    uint8_t index;
    uint8_t total;
    uint8_t panel;
    bool revealed;
    bool reveal_allowed;
    bool have;
} MessageModel;

/* -------------------------------------------------------------- helpers ---- */

/*
 * Draw one line of plain text and underline the stretches of it that belong to
 * a person. Prefix widths are measured rather than assumed because the secondary
 * font is proportional - assuming a fixed advance puts the underline under the
 * wrong word, which on this screen would be an outright lie.
 */
static void msg_draw_underlined(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    const char* text,
    uint8_t start,
    uint8_t len,
    const PhmExposure* exposure) {
    char line[PHM_TEXT_MAX];
    if(len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, text + start, len);
    line[len] = '\0';
    canvas_draw_str(canvas, x, y, line);

    uint8_t run_start = 0;
    bool in_run = false;

    for(uint8_t i = 0; i <= len; i++) {
        bool sensitive = (i < len) && phm_privacy_is_sensitive(exposure, (uint8_t)(start + i));

        if(sensitive && !in_run) {
            run_start = i;
            in_run = true;
        } else if(!sensitive && in_run) {
            char prefix[PHM_TEXT_MAX];
            memcpy(prefix, line, run_start);
            prefix[run_start] = '\0';
            uint16_t x0 = canvas_string_width(canvas, prefix);

            memcpy(prefix, line, i);
            prefix[i] = '\0';
            uint16_t x1 = canvas_string_width(canvas, prefix);

            canvas_draw_line(
                canvas, (uint8_t)(x + x0), (uint8_t)(y + 2), (uint8_t)(x + x1), (uint8_t)(y + 2));
            in_run = false;
        }
    }
}

/* Greedy wrap at the last space that fits. */
static uint8_t msg_line_length(Canvas* canvas, const char* text, uint8_t start, uint8_t len,
                               uint8_t width) {
    char buffer[PHM_TEXT_MAX];
    uint8_t fit = 0;
    uint8_t last_space = 0;

    for(uint8_t i = 0; start + i < len && i < sizeof(buffer) - 1; i++) {
        buffer[i] = text[start + i];
        buffer[i + 1] = '\0';
        if(canvas_string_width(canvas, buffer) > width) break;
        fit = (uint8_t)(i + 1);
        if(buffer[i] == ' ') last_space = (uint8_t)(i + 1);
    }

    if(start + fit < len && last_space > 0) return last_space;
    return fit ? fit : 1u;
}

static void msg_draw_header(Canvas* canvas, const MessageModel* model) {
    char header[24];
    snprintf(
        header,
        sizeof(header),
        "%u/%u  %lu",
        (uint8_t)(model->index + 1u),
        model->total,
        (unsigned long)model->record.page.ric);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 8, header);
    canvas_draw_line(canvas, 0, 10, 127, 10);
}

/* ---------------------------------------------------------------- panels ---- */

static void msg_draw_redacted(Canvas* canvas, const MessageModel* model) {
    const PhmPage* page = &model->record.page;
    const PhmExposure* exposure = &model->record.exposure;

    if(page->len == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 24, "Tone only. It beeped and");
        canvas_draw_str(canvas, 0, 33, "nothing at all was said.");
        canvas_draw_str(canvas, 0, 45, "That still tells a listener");
        canvas_draw_str(canvas, 0, 54, "who was called, and when.");
        return;
    }

    if(model->revealed) {
        canvas_set_font(canvas, FontSecondary);
        uint8_t offset = 0;
        uint8_t y = 20;
        while(offset < page->len && y < 62) {
            uint8_t len = msg_line_length(canvas, page->text, offset, page->len, 106);
            msg_draw_underlined(canvas, 0, y, page->text, offset, len, exposure);
            offset = (uint8_t)(offset + len);
            y = (uint8_t)(y + 10);
        }
        return;
    }

    /* Three rows of skyline is ninety-six characters, which is every page the
     * decoder can hold. */
    uint8_t capacity = phm_redact_capacity(104);
    uint8_t y = 15;
    for(uint8_t row = 0; row < 3; row++) {
        uint8_t offset = (uint8_t)(row * capacity);
        if(offset >= page->len) break;
        phm_draw_redact_bar(canvas, 0, y, 104, page->text, page->len, offset, exposure);
        y = (uint8_t)(y + 10);
    }

    canvas_set_font(canvas, FontSecondary);
    char stat[40];
    snprintf(
        stat,
        sizeof(stat),
        "%u/%u chars personal",
        exposure->redacted_chars,
        exposure->chars);
    canvas_draw_str(canvas, 0, 51, stat);
}

static void msg_draw_grade(Canvas* canvas, const MessageModel* model) {
    const PhmExposure* exposure = &model->record.exposure;

    canvas_set_font(canvas, FontSecondary);

    char score[32];
    snprintf(score, sizeof(score), "Exposure %u of 100", exposure->score);
    canvas_draw_str(canvas, 0, 20, score);

    canvas_draw_str(canvas, 0, 31, "Capped by:");

    /* The floor reason is the honest half of the grade, so it gets the room to
     * be a sentence rather than a code. */
    const char* reason = phm_floor_reason(exposure->floor);
    uint8_t offset = 0;
    uint8_t len = (uint8_t)strlen(reason);
    uint8_t y = 41;
    while(offset < len && y < 62) {
        uint8_t take = msg_line_length(canvas, reason, offset, len, 106);
        char line[64];
        uint8_t n = (take < sizeof(line) - 1) ? take : (uint8_t)(sizeof(line) - 1);
        memcpy(line, reason + offset, n);
        line[n] = '\0';
        canvas_draw_str(canvas, 0, y, line);
        offset = (uint8_t)(offset + take);
        y = (uint8_t)(y + 9);
    }
}

static void msg_draw_signal(Canvas* canvas, const MessageModel* model) {
    const PhmRecord* record = &model->record;
    const PhmPage* page = &record->page;

    canvas_set_font(canvas, FontSecondary);
    char line[40];

    snprintf(line, sizeof(line), "Frame %lu of 8", (unsigned long)(page->ric & 7u));
    canvas_draw_str(canvas, 0, 20, line);

    snprintf(
        line,
        sizeof(line),
        "Function %u   %s",
        (uint8_t)(page->func & 3u),
        phm_page_kind_name(page->kind));
    canvas_draw_str(canvas, 0, 29, line);

    snprintf(
        line,
        sizeof(line),
        "%s bps   %lu.%02lu MHz",
        phm_bauds[record->baud_idx % PHM_BAUD_COUNT].label,
        (unsigned long)(record->frequency / 1000000u),
        (unsigned long)((record->frequency % 1000000u) / 10000u));
    canvas_draw_str(canvas, 0, 38, line);

    snprintf(line, sizeof(line), "%u bits repaired by BCH", page->errors);
    canvas_draw_str(canvas, 0, 47, line);

    if(page->bad_words) {
        snprintf(line, sizeof(line), "%u words lost  %d dBm", page->bad_words, record->rssi);
    } else {
        snprintf(line, sizeof(line), "nothing lost  %d dBm", record->rssi);
    }
    canvas_draw_str(canvas, 0, 56, line);
}

/* ----------------------------------------------------------------- draw ---- */

static void message_draw(Canvas* canvas, void* context) {
    const MessageModel* model = context;

    canvas_clear(canvas);

    if(!model->have) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No pages captured");
        return;
    }

    msg_draw_header(canvas, model);
    phm_draw_grade_badge(canvas, 110, 13, model->record.exposure.grade);

    switch(model->panel) {
    case MsgPanelGrade:
        msg_draw_grade(canvas, model);
        break;
    case MsgPanelSignal:
        msg_draw_signal(canvas, model);
        break;
    default:
        msg_draw_redacted(canvas, model);
        break;
    }

    canvas_draw_line(canvas, 0, 53, 127, 53);
    canvas_set_font(canvas, FontSecondary);

    if(model->panel == MsgPanelRedacted) {
        phm_draw_leak_list(canvas, 0, 62, 80, model->record.exposure.flags);
        if(model->record.page.len) {
            canvas_draw_str_aligned(
                canvas, 127, 62, AlignRight, AlignBottom,
                model->revealed ? "OK hide" : "OK hold");
        }
    } else {
        static const char* const names[MSG_PANEL_COUNT] = {"Redacted", "Grade", "Signal"};
        canvas_draw_str(canvas, 0, 62, names[model->panel % MSG_PANEL_COUNT]);
        phm_draw_leak_list(canvas, 44, 62, 83, model->record.exposure.flags);
    }
}

static bool message_input(InputEvent* event, void* context) {
    MessageView* view = context;
    bool handled = false;

    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:
            with_view_model(
                view->view,
                MessageModel * model,
                {
                    model->panel = (uint8_t)((model->panel + MSG_PANEL_COUNT - 1u) % MSG_PANEL_COUNT);
                    model->revealed = false;
                },
                true);
            handled = true;
            break;
        case InputKeyDown:
            with_view_model(
                view->view,
                MessageModel * model,
                {
                    model->panel = (uint8_t)((model->panel + 1u) % MSG_PANEL_COUNT);
                    model->revealed = false;
                },
                true);
            handled = true;
            break;
        case InputKeyLeft:
            if(view->step_callback) view->step_callback(view->context, -1);
            handled = true;
            break;
        case InputKeyRight:
            if(view->step_callback) view->step_callback(view->context, 1);
            handled = true;
            break;
        case InputKeyOk:
            /* A short press only ever hides. Showing takes a long one. */
            with_view_model(
                view->view, MessageModel * model, { model->revealed = false; }, true);
            handled = true;
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        with_view_model(
            view->view,
            MessageModel * model,
            {
                if(model->reveal_allowed && model->panel == MsgPanelRedacted) {
                    model->revealed = !model->revealed;
                }
            },
            true);
        handled = true;
    }

    return handled;
}

/* ------------------------------------------------------------ lifecycle ---- */

MessageView* message_view_alloc(void) {
    MessageView* view = malloc(sizeof(MessageView));
    memset(view, 0, sizeof(MessageView));

    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(MessageModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, message_draw);
    view_set_input_callback(view->view, message_input);

    return view;
}

void message_view_free(MessageView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* message_view_get_view(MessageView* view) {
    furi_assert(view);
    return view->view;
}

void message_view_set_record(
    MessageView* view,
    const PhmRecord* record,
    uint8_t index,
    uint8_t total) {
    furi_assert(view);
    with_view_model(
        view->view,
        MessageModel * model,
        {
            if(record) {
                model->record = *record;
                model->have = true;
            } else {
                model->have = false;
            }
            model->index = index;
            model->total = total;
            /* Moving to another page always re-hides it. Revealing one message
             * is not consent to reveal the next. */
            model->revealed = false;
        },
        true);
}

void message_view_set_reveal_allowed(MessageView* view, bool allowed) {
    furi_assert(view);
    with_view_model(
        view->view,
        MessageModel * model,
        {
            model->reveal_allowed = allowed;
            if(!allowed) model->revealed = false;
        },
        true);
}

void message_view_set_step_callback(
    MessageView* view,
    MessageStepCallback callback,
    void* context) {
    furi_assert(view);
    view->step_callback = callback;
    view->context = context;
}
