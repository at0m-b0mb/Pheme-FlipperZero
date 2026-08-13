#include "phm_bch.h"

#include <stdlib.h>
#include <string.h>

uint8_t phm_popcount32(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0F0F0F0Fu;
    return (uint8_t)((v * 0x01010101u) >> 24);
}

bool phm_bch_parity_odd(uint32_t word) {
    return (phm_popcount32(word) & 1u) != 0u;
}

/*
 * Long division of the 31-bit code polynomial by g(x). Bit 30 of code31 is the
 * x^30 coefficient, bit 0 is the constant term.
 */
uint32_t phm_bch_syndrome(uint32_t code31) {
    uint32_t s = code31 & 0x7FFFFFFFu;
    for(int i = 30; i >= 10; i--) {
        if(s & (1u << i)) s ^= PHM_BCH_POLY << (i - 10);
    }
    return s & 0x3FFu;
}

uint32_t phm_bch_encode(uint32_t info21) {
    uint32_t code31 = (info21 & 0x1FFFFFu) << 10;
    code31 |= phm_bch_syndrome(code31);

    uint32_t word = code31 << 1;
    if(phm_bch_parity_odd(word)) word |= 1u;
    return word;
}

PhmBchTable* phm_bch_table_alloc(void) {
    PhmBchTable* table = malloc(sizeof(PhmBchTable));
    if(!table) return NULL;
    memset(table, 0, sizeof(PhmBchTable));

    /* Single-bit errors first: their syndromes are distinct from each other and
     * from every double-bit syndrome, so nothing here can be overwritten. */
    for(uint8_t i = 0; i < 31; i++) {
        uint32_t pattern = 1u << i;
        uint32_t s = phm_bch_syndrome(pattern);
        table->pattern[s] = pattern;
        table->weight[s] = 1;
    }

    for(uint8_t i = 0; i < 31; i++) {
        for(uint8_t j = (uint8_t)(i + 1); j < 31; j++) {
            uint32_t pattern = (1u << i) | (1u << j);
            uint32_t s = phm_bch_syndrome(pattern);
            /* Guarded rather than assumed: a lighter pattern always wins, so a
             * table built on a mistaken polynomial degrades instead of lying. */
            if(table->weight[s] == 0 || table->weight[s] > 2) {
                table->pattern[s] = pattern;
                table->weight[s] = 2;
            }
        }
    }

    return table;
}

void phm_bch_table_free(PhmBchTable* table) {
    free(table);
}

PhmBchResult phm_bch_correct(const PhmBchTable* table, uint32_t* word, uint8_t* errs) {
    if(!table || !word) return PhmBchUncorrectable;

    uint32_t received = *word;
    uint32_t code31 = received >> 1;
    uint8_t changed = 0;

    uint32_t s = phm_bch_syndrome(code31);
    if(s != 0) {
        uint8_t weight = table->weight[s];
        if(weight == 0) {
            if(errs) *errs = 3;
            return PhmBchUncorrectable;
        }
        code31 ^= table->pattern[s];
        changed = weight;
        received = (code31 << 1) | (received & 1u);
    }

    /*
     * The overall parity bit is outside the BCH code, so it gets checked last.
     * A word whose syndrome is clean but whose parity is odd has exactly one
     * error and it is in the parity bit itself. A word that needed BCH repair
     * and *still* fails parity took at least one more hit than the code can
     * account for, and three changes is past what distance 6 justifies.
     */
    if(phm_bch_parity_odd(received)) {
        received ^= 1u;
        changed++;
    }

    if(changed > 2) {
        if(errs) *errs = changed;
        return PhmBchUncorrectable;
    }

    *word = received;
    if(errs) *errs = changed;
    return changed ? PhmBchCorrected : PhmBchClean;
}
