/*
 * phm_bch - the error correction that POCSAG *does* have.
 *
 * Every POCSAG codeword is 32 bits: 21 information bits, a 10-bit BCH(31,21)
 * check, and one overall even parity bit. That is eleven bits of every
 * thirty-two spent defending the message against radio noise - and none at all
 * spent defending it against a listener. Pheme implements the code properly so
 * that the point can be made honestly: the protocol's designers cared a great
 * deal about the message arriving intact, and not at all about who else read it.
 *
 * Generator polynomial g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 (0x769),
 * minimum distance 5 over the 31-bit code, so up to two bit errors are
 * correctable and three are detectable. The overall parity bit lifts the full
 * 32-bit word to distance 6.
 *
 * Deliberately free of every furi_ header: this file compiles unchanged on the
 * host so the correction tables can be tested exhaustively (see test/).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* g(x), with the x^10 term in bit 10. */
#define PHM_BCH_POLY 0x769u

/* Bit-reversal-free codeword constants, as transmitted, first bit in bit 31. */
#define PHM_CW_SYNC 0x7CD215D8u /* frame synchronisation codeword          */
#define PHM_CW_IDLE 0x7A89C197u /* idle codeword, pads unused frame slots  */

typedef enum {
    PhmBchClean, /* syndrome was zero and parity agreed        */
    PhmBchCorrected, /* one or two bits were repaired              */
    PhmBchUncorrectable, /* three or more errors: word must be dropped  */
} PhmBchResult;

/*
 * Syndrome -> error pattern lookup. 1024 entries covering the zero syndrome,
 * all 31 single-bit patterns and all 465 double-bit patterns; the remaining
 * slots stay at weight 0, which is how an uncorrectable word is recognised.
 * Roughly 5 KB, so it lives on the heap and is built once at startup rather
 * than being searched per codeword - correction runs inside the Sub-GHz worker
 * callback, which cannot afford 496 syndrome evaluations per word.
 */
typedef struct {
    uint32_t pattern[1024]; /* bits 0..30 of the 31-bit code */
    uint8_t weight[1024]; /* 0 = no such correctable pattern */
} PhmBchTable;

/** Build the table. Returns NULL only if allocation failed. */
PhmBchTable* phm_bch_table_alloc(void);
void phm_bch_table_free(PhmBchTable* table);

/** Remainder of a 31-bit code polynomial modulo g(x). Zero means valid. */
uint32_t phm_bch_syndrome(uint32_t code31);

/** Even parity of a 32-bit word: true when an odd number of bits are set. */
bool phm_bch_parity_odd(uint32_t word);

/**
 * Encode 21 information bits (in the low 21 bits, most significant first as
 * transmitted) into a complete 32-bit codeword. Used by the generator that
 * feeds demo mode and the round-trip tests.
 */
uint32_t phm_bch_encode(uint32_t info21);

/**
 * Repair a received codeword in place.
 *
 * @param word  the 32 received bits, first bit received in bit 31
 * @param errs  out, number of bit positions changed (0..3)
 */
PhmBchResult phm_bch_correct(const PhmBchTable* table, uint32_t* word, uint8_t* errs);

/** Population count, exposed because the sync hunt measures Hamming distance. */
uint8_t phm_popcount32(uint32_t v);

#ifdef __cplusplus
}
#endif
