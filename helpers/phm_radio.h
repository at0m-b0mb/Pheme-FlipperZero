/*
 * phm_radio - the CC1101, and everything that has to happen on a thread.
 *
 * Receive only. Pheme has no transmit path at all: there is no code in this
 * repository that puts a carrier on a paging channel, and there is deliberately
 * no encoder wired to the radio even though one exists for the demo.
 *
 * What the Flipper can and cannot hear is worth saying plainly, because it is
 * the first question anybody asks and the honest answer is "less than you
 * think". The CC1101 covers 300-348, 387-464 and 779-928 MHz. That reaches the
 * on-site and amateur paging allocations, and a slice of the US 454 MHz band.
 * It does not reach VHF paging at 138-174 MHz, it does not reach UK and European
 * commercial paging at 466 MHz - two megahertz above the chip's upper edge -
 * and it does not reach US national paging at 929-932 MHz. Those channels carry
 * a great deal of what this app is about, and Pheme says so rather than
 * pretending an empty screen means an empty band.
 */
#pragma once

#include "phm_pocsag.h"
#include "phm_roster.h"

#include <gui/view_dispatcher.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frequency;
    const char* label; /* six characters, for the header      */
    const char* use;
} PhmChannel;

/* Paging allocations the CC1101 can actually tune. */
#define PHM_CHANNEL_COUNT   8
#define PHM_CHANNEL_DEFAULT 1 /* 439.9875, the worldwide amateur POCSAG channel */

extern const PhmChannel phm_channels[PHM_CHANNEL_COUNT];

/* Paging allocations it cannot, and why. Shown on the coverage screen so an
 * empty band is never mistaken for a quiet one. */
typedef struct {
    const char* label;
    const char* use;
    const char* why;
} PhmBlindSpot;

#define PHM_BLIND_COUNT 4
extern const PhmBlindSpot phm_blind_spots[PHM_BLIND_COUNT];

typedef struct {
    uint32_t frequency;
    uint16_t batches;
    uint16_t pages;
    int8_t peak_dbm;
    bool seen;
} PhmScanBand;

typedef struct PhmRadio PhmRadio;

PhmRadio* phm_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t page_event);
void phm_radio_free(PhmRadio* radio);

void phm_radio_configure(PhmRadio* radio, uint8_t channel_idx, int8_t baud_lock, bool narrow);

/** Clear the spool, the roster and the decoder counters. */
void phm_radio_reset_session(PhmRadio* radio);

void phm_radio_listen_start(PhmRadio* radio);
void phm_radio_listen_stop(PhmRadio* radio);
bool phm_radio_is_listening(const PhmRadio* radio);

/**
 * Demo mode. Generates a paging channel in software and feeds it through the
 * real decoder - the radio is never powered on, and the pages that appear have
 * been through BCH, framing and grading exactly as a live capture would.
 */
void phm_radio_demo_start(PhmRadio* radio);
void phm_radio_demo_stop(PhmRadio* radio);
bool phm_radio_is_demo(const PhmRadio* radio);

/** Hop the channel list looking for POCSAG framing, not just for energy. */
void phm_radio_scan_start(PhmRadio* radio);
void phm_radio_scan_stop(PhmRadio* radio);
bool phm_radio_is_scanning(const PhmRadio* radio);
uint8_t phm_radio_scan_snapshot(const PhmRadio* radio, PhmScanBand* out, uint8_t max);
uint32_t phm_radio_scan_sweeps(const PhmRadio* radio);
int8_t phm_radio_scan_best(const PhmRadio* radio);

/** Stop whatever is running, whichever mode it is. */
void phm_radio_stop_all(PhmRadio* radio);

void phm_radio_status(const PhmRadio* radio, PhmPocsagStatus* out);
float phm_radio_rssi(const PhmRadio* radio);
uint32_t phm_radio_frequency(const PhmRadio* radio);
uint32_t phm_radio_elapsed_ms(const PhmRadio* radio);

/** Copy a page out of the ring under the lock. False if the index is empty. */
bool phm_radio_page_at(const PhmRadio* radio, uint8_t index, PhmRecord* out);
uint8_t phm_radio_page_count(const PhmRadio* radio);
bool phm_radio_worst_page(const PhmRadio* radio, PhmRecord* out);

/** Snapshot the capcode table, busiest first. */
uint8_t phm_radio_roster_snapshot(const PhmRadio* radio, PhmPager* out, uint8_t max);

/** Capcodes heard after the roster filled up, so the log can say so. */
uint16_t phm_radio_roster_overflow(const PhmRadio* radio);
void phm_radio_tally(const PhmRadio* radio, PhmTally* out);

#ifdef __cplusplus
}
#endif
