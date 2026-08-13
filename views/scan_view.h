#pragma once

#include "../helpers/phm_radio.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScanView ScanView;

typedef void (*ScanPickCallback)(void* context, uint8_t channel_idx);

ScanView* scan_view_alloc(void);
void scan_view_free(ScanView* view);
View* scan_view_get_view(ScanView* view);

void scan_view_set_data(
    ScanView* view,
    const PhmScanBand* bands,
    uint8_t count,
    uint32_t sweeps,
    uint8_t current);

/** OK tunes the highlighted channel and starts listening on it. */
void scan_view_set_pick_callback(ScanView* view, ScanPickCallback callback, void* context);

#ifdef __cplusplus
}
#endif
