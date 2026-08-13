#pragma once

#include "../helpers/phm_pocsag.h"
#include "../helpers/phm_privacy.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ListenView ListenView;

/* Everything the screen shows, copied out from under the radio's lock once per
 * redraw so the draw callback never touches live state. */
typedef struct {
    const char* channel;
    const char* use;
    bool demo;

    uint8_t lock; /* PhmLockState                */
    int8_t baud_idx;
    uint32_t batches;
    uint32_t pages;
    uint8_t capcodes;
    uint32_t bad_words;
    uint8_t frame_mask;
    uint8_t word_idx;
    int8_t rssi;
    uint32_t elapsed_s;

    bool have_page;
    uint32_t ric;
    uint8_t kind;
    uint8_t len;
    char text[PHM_TEXT_MAX];
    PhmExposure exposure;
} ListenData;

typedef void (*ListenCallback)(void* context);

ListenView* listen_view_alloc(void);
void listen_view_free(ListenView* view);
View* listen_view_get_view(ListenView* view);

void listen_view_set_data(ListenView* view, const ListenData* data);
void listen_view_tick(ListenView* view);

/** OK opens the page list; only fires when there is something to open. */
void listen_view_set_open_callback(ListenView* view, ListenCallback callback, void* context);

#ifdef __cplusplus
}
#endif
