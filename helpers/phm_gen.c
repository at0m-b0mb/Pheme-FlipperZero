#include "phm_gen.h"

#include "phm_bch.h"
#include "phm_pocsag.h"

#include <string.h>

#define PHM_GEN_PREAMBLE_BITS 576
#define PHM_GEN_MAX_BATCHES   8

typedef struct {
    PhmGenEmit emit;
    void* context;
    uint16_t bit_us;

    bool started;
    bool level;
    uint32_t run_bits;

    bool damage_armed;
    uint16_t damage_period;
    uint32_t damage_seed;
} GenState;

/*
 * Bit errors are scattered at random with the requested mean spacing rather
 * than dropped on every Nth bit. Uniform damage turns out to be untestable:
 * with one flip every N bits, the 32-bit sync codeword takes the same beating
 * as every other codeword, so there is no setting at which words start failing
 * while synchronisation survives. Real radio errors cluster, which is precisely
 * how a receiver ends up locked onto a batch it cannot fully read.
 */
static bool gen_damage(GenState* g) {
    g->damage_seed = (g->damage_seed * 1103515245u) + 12345u;
    uint32_t r = (g->damage_seed >> 16) & 0x7FFFu;
    return (r % g->damage_period) == 0u;
}

/* Bits go out one at a time and are coalesced into runs on the way, so a whole
 * transmission never needs a buffer - which matters, because demo mode runs on
 * the Flipper's heap alongside everything else. */
static void gen_bit(GenState* g, bool bit) {
    if(g->damage_armed && g->damage_period && gen_damage(g)) bit = !bit;

    if(!g->started) {
        g->started = true;
        g->level = bit;
        g->run_bits = 1;
        return;
    }

    if(bit == g->level) {
        g->run_bits++;
        return;
    }

    g->emit(g->context, g->level, g->run_bits * g->bit_us);
    g->level = bit;
    g->run_bits = 1;
}

static void gen_flush(GenState* g) {
    if(g->started && g->run_bits) {
        g->emit(g->context, g->level, g->run_bits * g->bit_us);
    }
    g->started = false;
    g->run_bits = 0;
}

static void gen_word(GenState* g, uint32_t word) {
    for(int8_t i = 31; i >= 0; i--) {
        gen_bit(g, (word >> i) & 1u);
    }
}

/* --------------------------------------------------------- payload bits ---- */

typedef struct {
    const char* text;
    bool numeric;
    uint16_t pos;
    uint8_t bit;
} TextBits;

static const char* phm_gen_numeric_charset(void) {
    return "0123456789*U -)(";
}

static uint8_t numeric_code(char c) {
    const char* table = phm_gen_numeric_charset();
    for(uint8_t i = 0; i < 16; i++) {
        if(table[i] == c) return i;
    }
    return 12; /* anything unrepresentable becomes a space */
}

static bool text_exhausted(const TextBits* t) {
    return !t->text || t->text[t->pos] == '\0';
}

/*
 * Characters go out least significant bit first, both alphabets.
 *
 * Twenty bits rarely divide evenly into characters, so the tail of the last
 * codeword is filler. The filler matters: a receiver has no way to tell it from
 * content, so a terminal that pads numeric messages with zeros makes every
 * callback number end in a string of noughts. The convention is to pad with the
 * space character, and the alphanumeric alphabet pads with NUL - which is what
 * lets a receiver find the end of the text at all.
 */
static bool text_next_bit(TextBits* t) {
    uint8_t width = t->numeric ? 4u : 7u;
    uint8_t value;

    if(text_exhausted(t)) {
        value = t->numeric ? 12u : 0u; /* space, or NUL */
    } else {
        char c = t->text[t->pos];
        value = t->numeric ? numeric_code(c) : (uint8_t)(c & 0x7Fu);
    }

    bool bit = ((value >> t->bit) & 1u) != 0u;

    if(++t->bit >= width) {
        t->bit = 0;
        if(!text_exhausted(t)) t->pos++;
    }
    return bit;
}

/* Fill one 20-bit message codeword payload. False when there was nothing left
 * to put in it. */
static bool text_take20(TextBits* t, uint32_t* out) {
    if(text_exhausted(t) && t->bit == 0) return false;

    uint32_t data = 0;
    for(uint8_t i = 0; i < 20; i++) {
        data = (data << 1) | (text_next_bit(t) ? 1u : 0u);
    }
    *out = data;
    return true;
}

/* --------------------------------------------------------------- pages ---- */

uint32_t phm_gen_address_word(uint32_t ric, uint8_t func) {
    uint32_t info21 = (((ric >> 3) & 0x3FFFFu) << 2) | (func & 0x3u);
    return phm_bch_encode(info21);
}

static void gen_page(GenState* g, const PhmGenPage* page) {
    TextBits bits = {
        .text = page->text,
        .numeric = page->numeric,
        .pos = 0,
        .bit = 0,
    };

    uint8_t addr_frame = (uint8_t)(page->ric & 7u);
    uint32_t addr_word = phm_gen_address_word(page->ric, page->func);

    bool addr_sent = false;
    bool payload_done = (page->text == NULL) || (page->text[0] == '\0');

    for(uint8_t batch = 0; batch < PHM_GEN_MAX_BATCHES; batch++) {
        gen_word(g, PHM_CW_SYNC);

        for(uint8_t slot = 0; slot < 16; slot++) {
            uint8_t frame = (uint8_t)(slot >> 1);

            if(!addr_sent && frame == addr_frame && (slot & 1u) == 0u) {
                gen_word(g, addr_word);
                addr_sent = true;
            } else if(addr_sent && !payload_done) {
                uint32_t data20 = 0;
                if(text_take20(&bits, &data20)) {
                    gen_word(g, phm_bch_encode((1u << 20) | data20));
                } else {
                    payload_done = true;
                    gen_word(g, PHM_CW_IDLE);
                }
            } else {
                gen_word(g, PHM_CW_IDLE);
            }
        }

        if(addr_sent && payload_done) break;
    }
}

void phm_gen_transmission(
    const PhmGenPage* pages,
    uint8_t count,
    uint8_t baud_idx,
    uint16_t damage_period,
    uint32_t damage_seed,
    PhmGenEmit emit,
    void* context) {
    if(!pages || !count || !emit) return;
    if(baud_idx >= PHM_BAUD_COUNT) baud_idx = 1;

    GenState g;
    memset(&g, 0, sizeof(g));
    g.emit = emit;
    g.context = context;
    g.bit_us = phm_bauds[baud_idx].bit_us;
    g.damage_period = damage_period;
    g.damage_seed = damage_seed + 1u;

    for(uint16_t i = 0; i < PHM_GEN_PREAMBLE_BITS; i++) {
        gen_bit(&g, (i & 1u) == 0u);
    }

    /* Damage starts only once the preamble is behind us. Corrupting the wake-up
     * pattern would test the receiver's patience rather than its error
     * correction, which is not the point being made. */
    g.damage_armed = true;

    for(uint8_t i = 0; i < count; i++) {
        gen_page(&g, &pages[i]);
    }

    gen_flush(&g);

    /* Dead air, long enough that a receiver stops waiting for more. */
    emit(context, false, (uint32_t)phm_bauds[baud_idx].bit_us * (PHM_IDLE_RUN_BITS + 8u));
}

/* ----------------------------------------------------------- demo channel -- */

static const PhmGenPage phm_demo_pages[] = {
    {.ric = 1234567,
     .func = 3,
     .numeric = false,
     .text = "WARD 4B BED 12 MR A SAMPLE DOB 01/01/60 CRASH CALL RESUS TEAM ATTEND"},
    {.ric = 201456, .func = 0, .numeric = true, .text = "5550142"},
    {.ric = 1234567, .func = 3, .numeric = false, .text = "BLEEP 4471 CALL EXT 2210 SWITCHBOARD"},
    {.ric = 999001,
     .func = 3,
     .numeric = false,
     .text = "FIRE PANEL ZONE 3 PLANT ROOM LEVEL B2 INVESTIGATE"},
    {.ric = 555123, .func = 1, .numeric = false, .text = NULL},
    {.ric = 1234567,
     .func = 3,
     .numeric = false,
     .text = "PT J DOE NHS 4990000000 TRANSFER TO ITU BED 3"},
    {.ric = 777888,
     .func = 3,
     .numeric = false,
     .text = "DOOR CODE 4471 SIDE ENTRANCE 14 ELM ROAD"},
    {.ric = 201456, .func = 3, .numeric = false, .text = "STOCK DELIVERY BAY 2 SIGN FOR PALLETS"},
};

const PhmGenPage* phm_gen_demo_pages(uint8_t* count) {
    if(count) *count = (uint8_t)(sizeof(phm_demo_pages) / sizeof(phm_demo_pages[0]));
    return phm_demo_pages;
}
