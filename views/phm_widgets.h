/*
 * Drawing pieces shared by more than one screen.
 *
 * The important one is the redaction bar. A page is drawn as a row of blocks,
 * one per character: a short block for ordinary text and a full-height block
 * for anything the classifier decided belongs to a person. You cannot read it,
 * which is the point - but you can see at a glance how much of somebody's
 * message is somebody's data, and on a bad page the skyline is almost solid.
 */
#pragma once

#include "../helpers/phm_privacy.h"

#include <gui/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One character costs this many pixels across. */
#define PHM_REDACT_PITCH 3

/** Characters that fit in a bar of the given width. */
uint8_t phm_redact_capacity(uint8_t width);

/**
 * Draw a message as blocks. `offset` skips characters, so a long page can be
 * drawn across several rows.
 */
void phm_draw_redact_bar(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    const char* text,
    uint8_t len,
    uint8_t offset,
    const PhmExposure* exposure);

/** The grade, in a box. D and worse are drawn inverted so they carry weight. */
void phm_draw_grade_badge(Canvas* canvas, uint8_t x, uint8_t y, uint8_t grade);

/** Four bars, tallest for the strongest signal. Twelve pixels wide. */
void phm_draw_rssi(Canvas* canvas, uint8_t x, uint8_t y, int8_t dbm);

/** A comma-separated list of leak names, truncated to fit. */
void phm_draw_leak_list(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint16_t flags);

/** Eight slots, one per frame of a POCSAG batch; set bits are filled. */
void phm_draw_frame_ruler(Canvas* canvas, uint8_t x, uint8_t y, uint8_t mask, uint8_t cursor);

#ifdef __cplusplus
}
#endif
