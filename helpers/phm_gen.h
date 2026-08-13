/*
 * phm_gen - a POCSAG *transmitter*, on paper only.
 *
 * Nothing here ever touches the radio. It turns a page into the exact run-length
 * stream a real paging base station would put on the air, and hands it to a
 * callback. Two things use it:
 *
 *   - Demo mode, so the app can be seen working without a paging transmitter
 *     nearby, with every page travelling through the real decoder rather than
 *     being pasted into the UI.
 *   - The host tests, which round-trip text through encoder and decoder and
 *     then inject bit errors to prove the BCH correction is doing its job.
 *
 * Because the demo pages are decoded rather than displayed, the screenshots in
 * the README are produced by the same code path as a live capture. They cannot
 * drift away from what the app actually does.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ric; /* 21-bit capcode                       */
    uint8_t func; /* function bits, 0..3                  */
    const char* text; /* NULL or "" for a tone-only page      */
    bool numeric; /* encode with the 4-bit digit alphabet */
} PhmGenPage;

typedef void (*PhmGenEmit)(void* context, bool level, uint32_t duration_us);

/**
 * Put one transmission on the (imaginary) air: preamble, then as many batches
 * as the pages need, then a gap long enough for a receiver to call it over.
 *
 * @param damage_period  mean spacing of randomly scattered bit errors, counted
 *                       from the end of the preamble, or 0 for a clean signal.
 *                       Around 100 gives well under one error per codeword,
 *                       which BCH repairs silently; around 10 starts destroying
 *                       words outright.
 * @param damage_seed    makes the damage reproducible.
 */
void phm_gen_transmission(
    const PhmGenPage* pages,
    uint8_t count,
    uint8_t baud_idx,
    uint16_t damage_period,
    uint32_t damage_seed,
    PhmGenEmit emit,
    void* context);

/** Encode a single 21-bit information field into a complete codeword. */
uint32_t phm_gen_address_word(uint32_t ric, uint8_t func);

/**
 * The demo channel: eight pages of the kind that actually go out over paging
 * networks every day, in shape if not in substance. Every name, number and
 * address in them is invented - the telephone numbers are in the 555-01xx range
 * reserved for fiction - because the point is what the *format* leaks, and that
 * point does not need a real person's details to land.
 */
const PhmGenPage* phm_gen_demo_pages(uint8_t* count);

#ifdef __cplusplus
}
#endif
