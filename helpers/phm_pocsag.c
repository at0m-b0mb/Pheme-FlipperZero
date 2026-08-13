#include "phm_pocsag.h"

#include <stdlib.h>
#include <string.h>

/*
 * 512, 1200 and 2400 bits per second are the three rates POCSAG defines. The
 * bit periods are rounded to whole microseconds because that is the resolution
 * the capture path delivers; the error at 2400 bps is 0.08%, four hundred times
 * smaller than the slicer's own jitter.
 */
const PhmBaudSpec phm_bauds[PHM_BAUD_COUNT] = {
    {.baud = 512, .bit_us = 1953, .label = "512"},
    {.baud = 1200, .bit_us = 833, .label = "1200"},
    {.baud = 2400, .bit_us = 417, .label = "2400"},
};

/*
 * The numeric alphabet. Ten digits and six symbols, four bits each, sent least
 * significant bit first. Codes 10 and 11 are the spare and "urgent" characters,
 * 12 is a space and 13..15 are the hyphen and brackets used to lay out a
 * telephone number.
 */
static const char phm_numeric_charset[17] = "0123456789*U -)(";

typedef enum {
    LaneHunt,
    LaneBatch,
} LaneState;

typedef struct {
    uint32_t sr; /* sliding 32-bit window of received bits   */
    uint8_t state;
    uint8_t word_bits; /* bits accumulated into the current word   */
    uint8_t word_idx; /* 0..15, position in the batch             */
    uint8_t frame_mask; /* frames of this batch carrying an address */
    uint16_t alt_run; /* consecutive single-bit runs (preamble)   */
    uint32_t batches;

    /* the page being assembled */
    bool have_addr;
    uint32_t ric;
    uint8_t func;
    uint16_t payload_bits;
    uint8_t payload[PHM_MSG_BYTES];
    uint16_t errors;
    uint8_t bad_words;
    bool truncated;
} PhmLane;

struct PhmPocsag {
    PhmBchTable* bch;
    PhmLane lane[PHM_BAUD_COUNT];
    int8_t only_lane; /* -1 = all three          */
    int8_t locked; /* last lane to see a sync */

    uint32_t words;
    uint32_t bad_words;
    uint32_t corrected;
    uint32_t pages;
    uint32_t orphans;

    PhmPageCallback callback;
    void* context;

    /* Handed to the callback by pointer. The decoder runs inside the Sub-GHz
     * worker's 2 KB stack, which has no room for a page-sized local. */
    PhmPage scratch;
};

/* ------------------------------------------------------------- payload ---- */

static void lane_push_bit(PhmLane* lane, bool bit) {
    if(lane->payload_bits >= PHM_MSG_BYTES * 8) {
        lane->truncated = true;
        return;
    }
    if(bit) {
        lane->payload[lane->payload_bits >> 3] |= (uint8_t)(0x80u >> (lane->payload_bits & 7u));
    }
    lane->payload_bits++;
}

static bool payload_bit(const uint8_t* payload, uint16_t index) {
    return (payload[index >> 3] & (0x80u >> (index & 7u))) != 0u;
}

uint8_t phm_page_render(const PhmPage* page, PhmPageKind kind, char* out, uint8_t out_max) {
    if(!page || !out || out_max == 0) return 0;
    out[0] = '\0';
    if(kind == PhmPageTone || page->payload_bits == 0) return 0;

    uint8_t width = (kind == PhmPageAlpha) ? 7u : 4u;
    uint8_t len = 0;

    for(uint16_t i = 0; i + width <= page->payload_bits; i += width) {
        /* Characters are transmitted least significant bit first, so the group
         * is assembled by weight rather than by position. */
        uint8_t value = 0;
        for(uint8_t k = 0; k < width; k++) {
            if(payload_bit(page->payload, (uint16_t)(i + k))) value |= (uint8_t)(1u << k);
        }

        char c;
        if(kind == PhmPageAlpha) {
            if(value == 0) {
                /* NUL is the pad byte, and also what a dropped codeword leaves
                 * behind. Stop rather than printing filler as content. */
                break;
            }
            c = (value >= 0x20 && value < 0x7F) ? (char)value : '?';
        } else {
            c = phm_numeric_charset[value & 0x0Fu];
        }

        if(len + 1 >= out_max) break;
        out[len++] = c;
    }

    while(len > 0 && (out[len - 1] == ' ' || out[len - 1] == '?')) len--;
    out[len] = '\0';
    return len;
}

const char* phm_page_kind_name(uint8_t kind) {
    switch(kind) {
    case PhmPageTone:
        return "Tone";
    case PhmPageNumeric:
        return "Numeric";
    case PhmPageAlpha:
        return "Alpha";
    default:
        return "?";
    }
}

/*
 * Which alphabet was this page written in?
 *
 * The standard suggests function 0 for numeric and 3 for alphanumeric, and a
 * great many paging terminals honour that. A great many do not. So the function
 * bits are taken as a strong hint and the payload itself settles the argument:
 * a run of 7-bit groups that comes out as readable text almost certainly is.
 */
static uint8_t phm_choose_kind(const PhmPage* page, char* scratch, uint8_t scratch_max) {
    if(page->payload_bits < 7) return PhmPageTone;

    uint8_t alpha_len = phm_page_render(page, PhmPageAlpha, scratch, scratch_max);
    uint8_t letters = 0;
    for(uint8_t i = 0; i < alpha_len; i++) {
        char c = scratch[i];
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters++;
    }

    if(page->func == 3) return PhmPageAlpha;
    if(page->func == 0) return PhmPageNumeric;

    /* Functions 1 and 2 are site-specific. Readable text wins the tie. */
    if(alpha_len >= 3 && letters * 2 >= alpha_len) return PhmPageAlpha;
    return PhmPageNumeric;
}

/* --------------------------------------------------------------- lanes ---- */

static void lane_clear_message(PhmLane* lane) {
    lane->have_addr = false;
    lane->ric = 0;
    lane->func = 0;
    lane->payload_bits = 0;
    lane->errors = 0;
    lane->bad_words = 0;
    lane->truncated = false;
    memset(lane->payload, 0, sizeof(lane->payload));
}

static void lane_emit(PhmPocsag* pocsag, PhmLane* lane, uint8_t lane_idx) {
    if(!lane->have_addr) {
        lane_clear_message(lane);
        return;
    }

    PhmPage* page = &pocsag->scratch;
    memset(page, 0, sizeof(*page));
    page->ric = lane->ric;
    page->func = lane->func;
    page->baud_idx = lane_idx;
    page->errors = (lane->errors > 255) ? 255u : (uint8_t)lane->errors;
    page->bad_words = lane->bad_words;
    page->truncated = lane->truncated;
    page->payload_bits = lane->payload_bits;
    memcpy(page->payload, lane->payload, sizeof(page->payload));

    page->kind = phm_choose_kind(page, page->text, sizeof(page->text));
    page->len = phm_page_render(page, (PhmPageKind)page->kind, page->text, sizeof(page->text));

    pocsag->pages++;
    if(pocsag->callback) pocsag->callback(pocsag->context, page);

    lane_clear_message(lane);
}

static void lane_reset_framing(PhmLane* lane) {
    lane->state = LaneHunt;
    lane->word_bits = 0;
    lane->word_idx = 0;
    lane->frame_mask = 0;
}

/*
 * One complete 32-bit codeword has arrived at a known position in the batch.
 */
static void lane_word(PhmPocsag* pocsag, PhmLane* lane, uint8_t lane_idx, uint32_t raw) {
    uint32_t word = raw;
    uint8_t errs = 0;
    PhmBchResult result = phm_bch_correct(pocsag->bch, &word, &errs);

    pocsag->words++;
    if(result == PhmBchUncorrectable) {
        pocsag->bad_words++;
        if(lane->have_addr) {
            /*
             * A hole in the middle of a message. Twenty placeholder bits go in
             * anyway: dropping them silently would shift every following
             * character by five bit positions and turn one lost codeword into
             * an unreadable remainder. The count is reported so the UI can say
             * the page is damaged rather than quietly showing rubbish.
             */
            lane->bad_words++;
            for(uint8_t i = 0; i < 20; i++) lane_push_bit(lane, false);
        }
        return;
    }

    if(errs) {
        pocsag->corrected += errs;
        if(lane->have_addr) lane->errors += errs;
    }

    if(word == PHM_CW_IDLE) {
        lane_emit(pocsag, lane, lane_idx);
        return;
    }

    uint8_t frame = (uint8_t)(lane->word_idx >> 1);

    if((word & 0x80000000u) == 0u) {
        /* Address codeword: 18 address bits then 2 function bits. The bottom
         * three bits of the capcode are the frame number it arrived in. */
        lane_emit(pocsag, lane, lane_idx);

        uint32_t addr18 = (word >> 13) & 0x3FFFFu;
        lane->ric = (addr18 << 3) | frame;
        lane->func = (uint8_t)((word >> 11) & 0x3u);
        lane->have_addr = true;
        lane->frame_mask |= (uint8_t)(1u << frame);
        if(errs) lane->errors = errs;
    } else {
        if(!lane->have_addr) {
            /* A message body whose address codeword was in a batch we missed.
             * There is no way to know whose page it is, so it is counted and
             * discarded rather than attributed to the previous pager. */
            pocsag->orphans++;
            return;
        }
        uint32_t data20 = (word >> 11) & 0xFFFFFu;
        for(int8_t bit = 19; bit >= 0; bit--) {
            lane_push_bit(lane, (data20 >> bit) & 1u);
        }
    }
}

static void lane_bit(PhmPocsag* pocsag, PhmLane* lane, uint8_t lane_idx, bool bit) {
    lane->sr = (lane->sr << 1) | (bit ? 1u : 0u);

    /*
     * The sync codeword is tested on every single bit, in every state. A batch
     * boundary therefore re-establishes framing even after the decoder has been
     * knocked off it, which is the difference between losing one batch to a
     * glitch and losing the rest of the transmission.
     */
    if(phm_popcount32(lane->sr ^ PHM_CW_SYNC) <= PHM_SYNC_TOL) {
        lane->state = LaneBatch;
        lane->word_bits = 0;
        lane->word_idx = 0;
        lane->frame_mask = 0;
        lane->batches++;
        pocsag->locked = (int8_t)lane_idx;
        return;
    }

    if(lane->state != LaneBatch) return;

    if(++lane->word_bits < 32) return;
    lane->word_bits = 0;

    if(lane->word_idx >= 16) {
        /* Sixteen codewords went by and the sync that should have followed them
         * never matched. Lock is gone; hand back whatever was complete. */
        lane_emit(pocsag, lane, lane_idx);
        lane_reset_framing(lane);
        return;
    }

    lane_word(pocsag, lane, lane_idx, lane->sr);
    lane->word_idx++;
}

/* ------------------------------------------------------------ lifecycle ---- */

PhmPocsag* phm_pocsag_alloc(void) {
    PhmPocsag* pocsag = malloc(sizeof(PhmPocsag));
    if(!pocsag) return NULL;
    memset(pocsag, 0, sizeof(PhmPocsag));

    pocsag->bch = phm_bch_table_alloc();
    if(!pocsag->bch) {
        free(pocsag);
        return NULL;
    }

    pocsag->only_lane = -1;
    pocsag->locked = -1;
    return pocsag;
}

void phm_pocsag_free(PhmPocsag* pocsag) {
    if(!pocsag) return;
    phm_bch_table_free(pocsag->bch);
    free(pocsag);
}

void phm_pocsag_set_callback(PhmPocsag* pocsag, PhmPageCallback callback, void* context) {
    if(!pocsag) return;
    pocsag->callback = callback;
    pocsag->context = context;
}

void phm_pocsag_reset(PhmPocsag* pocsag) {
    if(!pocsag) return;
    for(uint8_t i = 0; i < PHM_BAUD_COUNT; i++) {
        memset(&pocsag->lane[i], 0, sizeof(PhmLane));
    }
    pocsag->locked = -1;
    pocsag->words = 0;
    pocsag->bad_words = 0;
    pocsag->corrected = 0;
    pocsag->pages = 0;
    pocsag->orphans = 0;
}

void phm_pocsag_set_lane(PhmPocsag* pocsag, int8_t baud_idx) {
    if(!pocsag) return;
    pocsag->only_lane = (baud_idx >= 0 && baud_idx < PHM_BAUD_COUNT) ? baud_idx : -1;
}

void phm_pocsag_feed_pair(PhmPocsag* pocsag, bool level, uint32_t duration_us) {
    if(!pocsag) return;

    for(uint8_t i = 0; i < PHM_BAUD_COUNT; i++) {
        if(pocsag->only_lane >= 0 && pocsag->only_lane != (int8_t)i) continue;

        PhmLane* lane = &pocsag->lane[i];
        uint16_t bit_us = phm_bauds[i].bit_us;

        /* Rounded, not truncated: a run is a whole number of bit periods and
         * the nearest one is the right guess. A run that rounds to zero is
         * shorter than a bit at this rate and is a glitch as far as this lane
         * is concerned. */
        uint32_t bits = (duration_us + (bit_us / 2u)) / bit_us;

        if(bits == 0) {
            lane->alt_run = 0;
            continue;
        }

        if(bits > PHM_IDLE_RUN_BITS) {
            /* Dead air. Whatever was half-assembled will never be completed. */
            lane_emit(pocsag, lane, i);
            lane_reset_framing(lane);
            lane->alt_run = 0;
            continue;
        }

        /* The preamble is 576 bits of 1010..., which at the correct rate is a
         * long run of single-bit runs and at any other rate is not. */
        if(bits == 1) {
            if(lane->alt_run < 0xFFFFu) lane->alt_run++;
        } else {
            lane->alt_run = 0;
        }

        for(uint32_t b = 0; b < bits; b++) {
            lane_bit(pocsag, lane, i, level);
        }
    }
}

void phm_pocsag_flush(PhmPocsag* pocsag) {
    if(!pocsag) return;
    for(uint8_t i = 0; i < PHM_BAUD_COUNT; i++) {
        lane_emit(pocsag, &pocsag->lane[i], i);
        lane_reset_framing(&pocsag->lane[i]);
    }
}

void phm_pocsag_status(const PhmPocsag* pocsag, PhmPocsagStatus* out) {
    if(!pocsag || !out) return;
    memset(out, 0, sizeof(*out));

    out->baud_idx = pocsag->locked;
    out->words = pocsag->words;
    out->bad_words = pocsag->bad_words;
    out->corrected = pocsag->corrected;
    out->pages = pocsag->pages;
    out->orphans = pocsag->orphans;

    uint32_t batches = 0;
    uint8_t lock = PhmLockIdle;
    for(uint8_t i = 0; i < PHM_BAUD_COUNT; i++) {
        const PhmLane* lane = &pocsag->lane[i];
        batches += lane->batches;

        if(lane->state == LaneBatch) {
            if(lock < PhmLockBatch) {
                lock = PhmLockBatch;
                out->frame_mask = lane->frame_mask;
                out->word_idx = lane->word_idx;
            }
        } else if(lane->batches && lock < PhmLockSync) {
            lock = PhmLockSync;
        } else if(lane->alt_run >= 32 && lock < PhmLockPreamble) {
            lock = PhmLockPreamble;
        }
    }

    out->lock = lock;
    out->batches = batches;
}
