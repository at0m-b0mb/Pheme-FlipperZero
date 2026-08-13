/*
 * phm_pocsag - a POCSAG receiver, written from scratch.
 *
 * The Flipper firmware's Sub-GHz protocol stack has no POCSAG decoder, so this
 * is the whole thing: bit-clock recovery from the CC1101's sliced output, the
 * 576-bit preamble, the frame synchronisation codeword, batch framing, BCH
 * repair, address/message codeword assembly and both payload alphabets.
 *
 * Structure of the air interface, for the reader who has not met it:
 *
 *   preamble            576 bits of 1010...     (lets a sleeping pager wake up)
 *   sync codeword       0x7CD215D8              (32 bits, marks a batch)
 *   batch               8 frames x 2 codewords  (512 bits)
 *   sync codeword       ...and so on
 *
 * A pager's address (its RIC, or capcode) is 21 bits. Only the top 18 travel in
 * the address codeword; the bottom 3 are the *frame number* the codeword landed
 * in. That is the whole trick that lets a pager sleep through seven eighths of
 * every batch, and it is why a receiver has to track framing to read addresses
 * at all.
 *
 * Three decoders run in parallel, one per standard rate (512, 1200, 2400 bps).
 * Each converts the same run-length stream into bits using its own bit period
 * and hunts for the sync word; whichever one locks tells you the rate. This is
 * cheaper and far more robust than trying to measure the rate first, because
 * misreading the rate by a factor of two is exactly the mistake that a
 * histogram of run lengths makes.
 *
 * No furi_ headers: this compiles on the host, so the decoder under test is
 * literally the decoder that ships.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "phm_bch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHM_BAUD_COUNT 3
#define PHM_MSG_BYTES  80 /* 640 payload bits: 91 alpha chars or 160 digits */
#define PHM_TEXT_MAX   96

/* Hamming distance tolerated when matching the 32-bit sync codeword. Two is
 * what the code itself is built to survive, and the odds of 32 arbitrary bits
 * landing within two of the sync pattern are about one in eight million. */
#define PHM_SYNC_TOL 2

/* A run longer than this many bit periods is silence, not data. No codeword can
 * contain a run near it, so nothing legitimate is lost by resetting on one. */
#define PHM_IDLE_RUN_BITS 64

typedef enum {
    PhmPageTone, /* address only: the pager beeped, nothing was said */
    PhmPageNumeric, /* 4-bit digits: a callback number, usually          */
    PhmPageAlpha, /* 7-bit ASCII: free text, and the interesting case  */
} PhmPageKind;

typedef enum {
    PhmLockIdle, /* nothing on the channel                            */
    PhmLockPreamble, /* alternating bits: something is waking pagers up   */
    PhmLockSync, /* sync codeword matched                             */
    PhmLockBatch, /* tracking batch framing                            */
} PhmLockState;

/* One page, as received. The raw payload is kept alongside the rendered text so
 * the UI can re-render it under the other alphabet - real paging terminals do
 * not always set the function bits the way the standard suggests. */
typedef struct {
    uint32_t ric; /* 21-bit capcode                        */
    uint8_t func; /* 0..3, the two function bits           */
    uint8_t baud_idx; /* which lane decoded it                 */
    uint8_t kind; /* PhmPageKind, auto-selected            */
    uint8_t errors; /* bits repaired by BCH across the page  */
    uint8_t bad_words; /* codewords past saving                 */
    bool truncated; /* payload buffer filled up              */
    uint16_t payload_bits;
    uint8_t payload[PHM_MSG_BYTES];
    char text[PHM_TEXT_MAX];
    uint8_t len;
} PhmPage;

typedef struct {
    uint16_t baud;
    uint16_t bit_us;
    const char* label;
} PhmBaudSpec;

extern const PhmBaudSpec phm_bauds[PHM_BAUD_COUNT];

typedef struct {
    uint8_t lock; /* PhmLockState                          */
    int8_t baud_idx; /* locked lane, or -1                    */
    uint32_t batches;
    uint32_t words;
    uint32_t bad_words;
    uint32_t corrected;
    uint32_t pages;
    uint32_t orphans; /* message words with no address ahead   */
    uint8_t frame_mask; /* frames of the current batch addressed */
    uint8_t word_idx; /* position within the current batch     */
} PhmPocsagStatus;

typedef struct PhmPocsag PhmPocsag;
typedef void (*PhmPageCallback)(void* ctx, const PhmPage* page);

PhmPocsag* phm_pocsag_alloc(void);
void phm_pocsag_free(PhmPocsag* pocsag);
void phm_pocsag_set_callback(PhmPocsag* pocsag, PhmPageCallback callback, void* context);
void phm_pocsag_reset(PhmPocsag* pocsag);

/** Restrict decoding to one rate, or -1 to run all three. */
void phm_pocsag_set_lane(PhmPocsag* pocsag, int8_t baud_idx);

/** One level and its duration, straight off the radio. */
void phm_pocsag_feed_pair(PhmPocsag* pocsag, bool level, uint32_t duration_us);

/** End of transmission: emit anything still being assembled. */
void phm_pocsag_flush(PhmPocsag* pocsag);

void phm_pocsag_status(const PhmPocsag* pocsag, PhmPocsagStatus* out);

/** Render a stored payload under a chosen alphabet. Returns the length. */
uint8_t phm_page_render(const PhmPage* page, PhmPageKind kind, char* out, uint8_t out_max);

const char* phm_page_kind_name(uint8_t kind);

#ifdef __cplusplus
}
#endif
