/*
 * The protocol, end to end and on the host.
 *
 * Text goes through the encoder, becomes a run-length stream at a chosen rate,
 * goes through the decoder that ships on the Flipper, and has to come back out
 * as the same text with the same capcode. Then the same stream is damaged on
 * purpose, to show that the BCH code repairs what it claims to repair and
 * refuses what it cannot.
 */
#include "../helpers/phm_bch.h"
#include "../helpers/phm_gen.h"
#include "../helpers/phm_pocsag.h"
#include "phm_test.h"

#include <string.h>

/* ------------------------------------------------------------ collector ---- */

#define MAX_CAUGHT 16

typedef struct {
    PhmPage page[MAX_CAUGHT];
    uint8_t count;
} Caught;

static void on_page(void* ctx, const PhmPage* page) {
    Caught* c = ctx;
    if(c->count < MAX_CAUGHT) c->page[c->count++] = *page;
}

typedef struct {
    PhmPocsag* pocsag;
    uint32_t pairs;
    uint32_t total_us;
} Feeder;

static void on_pair(void* ctx, bool level, uint32_t duration_us) {
    Feeder* f = ctx;
    f->pairs++;
    f->total_us += duration_us;
    phm_pocsag_feed_pair(f->pocsag, level, duration_us);
}

/* Jitter every run by a signed percentage of a bit period, the way a real
 * slicer does when the transmitter's clock and ours disagree. */
typedef struct {
    Feeder feeder;
    int32_t jitter_us;
    uint32_t seed;
} JitterFeeder;

static uint32_t rnd(uint32_t* seed) {
    *seed = (*seed * 1103515245u) + 12345u;
    return (*seed >> 16) & 0x7FFFu;
}

static void on_pair_jitter(void* ctx, bool level, uint32_t duration_us) {
    JitterFeeder* j = ctx;
    int32_t offset = 0;
    if(j->jitter_us) {
        offset = (int32_t)(rnd(&j->seed) % (uint32_t)(2 * j->jitter_us + 1)) - j->jitter_us;
    }
    int64_t d = (int64_t)duration_us + offset;
    if(d < 1) d = 1;
    on_pair(&j->feeder, level, (uint32_t)d);
}

static uint8_t run_seeded(
    const PhmGenPage* pages,
    uint8_t count,
    uint8_t baud_idx,
    uint16_t damage,
    uint32_t seed,
    Caught* caught,
    PhmPocsagStatus* status) {
    memset(caught, 0, sizeof(*caught));

    PhmPocsag* pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(pocsag, on_page, caught);

    Feeder feeder = {.pocsag = pocsag, .pairs = 0, .total_us = 0};
    phm_gen_transmission(pages, count, baud_idx, damage, seed, on_pair, &feeder);
    phm_pocsag_flush(pocsag);

    if(status) phm_pocsag_status(pocsag, status);
    phm_pocsag_free(pocsag);
    return caught->count;
}

static uint8_t run(
    const PhmGenPage* pages,
    uint8_t count,
    uint8_t baud_idx,
    uint16_t damage,
    Caught* caught,
    PhmPocsagStatus* status) {
    return run_seeded(pages, count, baud_idx, damage, 1u, caught, status);
}

/* ------------------------------------------------------------------ BCH ---- */

static void test_bch(void) {
    printf("BCH(31,21) + overall parity\n");

    PhmBchTable* table = phm_bch_table_alloc();
    CHECK(table != NULL, "table alloc");
    if(!table) return;

    /* The table must cover exactly the zero syndrome plus every one- and
     * two-bit pattern, and nothing else may claim a slot. */
    int singles = 0, doubles = 0, empty = 0;
    for(int i = 0; i < 1024; i++) {
        if(table->weight[i] == 1)
            singles++;
        else if(table->weight[i] == 2)
            doubles++;
        else if(table->weight[i] == 0)
            empty++;
        else
            CHECK(0, "syndrome %d has weight %u", i, table->weight[i]);
    }
    CHECK(singles == 31, "single-bit syndromes = %d, want 31", singles);
    CHECK(doubles == 465, "double-bit syndromes = %d, want 465", doubles);
    CHECK(singles + doubles + empty == 1024, "table accounts for every slot");

    /* Every distinct one- and two-bit error pattern must have a distinct
     * syndrome, or the code does not have distance 5 and correction is a lie. */
    for(uint8_t i = 0; i < 31; i++) {
        for(uint8_t j = (uint8_t)(i + 1); j < 31; j++) {
            uint32_t pattern = (1u << i) | (1u << j);
            uint32_t s = phm_bch_syndrome(pattern);
            CHECK(s != 0, "two-bit pattern %u,%u has zero syndrome", i, j);
            CHECK(table->pattern[s] == pattern, "syndrome collision at %u,%u", i, j);
        }
    }

    /* Encode/decode over the whole information space would be 2M words; a
     * decimated sweep of it, with every single- and double-bit error applied to
     * each, is 30k words and 15 million corrections. */
    int swept = 0;
    for(uint32_t info = 0; info < 0x200000u; info += 0x40u) {
        uint32_t clean = phm_bch_encode(info & 0x1FFFFFu);
        swept++;

        uint8_t errs = 0;
        uint32_t word = clean;
        CHECK(phm_bch_correct(table, &word, &errs) == PhmBchClean, "clean word rejected");
        CHECK(word == clean, "clean word altered");
        CHECK(errs == 0, "clean word reported %u errors", errs);
        CHECK(!phm_bch_parity_odd(clean), "encoded word has odd parity");
        CHECK((clean >> 11) == ((info & 0x1FFFFFu)), "encode lost information bits");

        for(uint8_t a = 0; a < 32; a++) {
            word = clean ^ (1u << a);
            errs = 0;
            CHECK(phm_bch_correct(table, &word, &errs) == PhmBchCorrected, "1-bit not corrected");
            CHECK(word == clean, "1-bit correction wrong at %u", a);
            CHECK(errs == 1, "1-bit reported %u errors", errs);
        }

        for(uint8_t a = 0; a < 32; a++) {
            for(uint8_t b = (uint8_t)(a + 1); b < 32; b++) {
                word = clean ^ (1u << a) ^ (1u << b);
                errs = 0;
                PhmBchResult r = phm_bch_correct(table, &word, &errs);
                CHECK(r == PhmBchCorrected, "2-bit not corrected at %u,%u", a, b);
                CHECK(word == clean, "2-bit correction wrong at %u,%u", a, b);
                CHECK(errs == 2, "2-bit reported %u errors at %u,%u", errs, a, b);
            }
        }
    }
    printf("  swept %d codewords\n", swept);

    /*
     * Three errors is past the code's distance. It must never silently produce
     * a different valid codeword; either it refuses, or it lands somewhere that
     * is provably not the original. The second case is the honest one to
     * measure, so count how often it happens rather than assuming it does not.
     */
    int refused = 0, mangled = 0, wrong_accept = 0;
    for(uint32_t info = 0; info < 0x200000u; info += 0x1000u) {
        uint32_t clean = phm_bch_encode(info & 0x1FFFFFu);
        for(uint8_t a = 0; a < 32; a++) {
            for(uint8_t b = (uint8_t)(a + 1); b < 32; b++) {
                for(uint8_t c = (uint8_t)(b + 1); c < 32; c++) {
                    uint32_t word = clean ^ (1u << a) ^ (1u << b) ^ (1u << c);
                    uint8_t errs = 0;
                    PhmBchResult r = phm_bch_correct(table, &word, &errs);
                    if(r == PhmBchUncorrectable)
                        refused++;
                    else if(word == clean)
                        wrong_accept++;
                    else
                        mangled++;
                }
            }
        }
    }
    CHECK(wrong_accept == 0, "3-bit errors 'corrected' back to the original %d times", wrong_accept);
    CHECK(refused > 0, "no 3-bit error was refused");
    printf("  3-bit errors: %d refused, %d miscorrected to another word\n", refused, mangled);

    phm_bch_table_free(table);
}

/* -------------------------------------------------------------- framing ---- */

static void test_roundtrip(void) {
    printf("\nEncoder -> air -> decoder\n");

    for(uint8_t baud = 0; baud < PHM_BAUD_COUNT; baud++) {
        const PhmGenPage pages[] = {
            {.ric = 1234567, .func = 3, .numeric = false, .text = "TEST MESSAGE 123"},
        };

        Caught caught;
        PhmPocsagStatus status;
        uint8_t n = run(pages, 1, baud, 0, &caught, &status);

        CHECK(n == 1, "%s bps: caught %u pages, want 1", phm_bauds[baud].label, n);
        if(n != 1) continue;

        CHECK(caught.page[0].ric == 1234567, "%s bps: ric %lu", phm_bauds[baud].label,
              (unsigned long)caught.page[0].ric);
        CHECK_STR(caught.page[0].text, "TEST MESSAGE 123");
        CHECK(caught.page[0].kind == PhmPageAlpha, "%s bps: kind", phm_bauds[baud].label);
        CHECK(caught.page[0].errors == 0, "%s bps: %u errors on a clean signal",
              phm_bauds[baud].label, caught.page[0].errors);
        CHECK(caught.page[0].bad_words == 0, "%s bps: %u bad words on a clean signal",
              phm_bauds[baud].label, caught.page[0].bad_words);

        /* The rate is an output, not an input: the lane that locked is the
         * answer, and it has to be the one that was transmitted. */
        CHECK(caught.page[0].baud_idx == baud, "%s bps: decoded as %s",
              phm_bauds[baud].label, phm_bauds[caught.page[0].baud_idx].label);
        CHECK(status.baud_idx == (int8_t)baud, "%s bps: status lane %d",
              phm_bauds[baud].label, status.baud_idx);
        CHECK(status.lock == PhmLockBatch || status.lock == PhmLockSync,
              "%s bps: lock state %u", phm_bauds[baud].label, status.lock);
    }
}

static void test_capcodes(void) {
    printf("\nCapcode reconstruction across all eight frames\n");

    /* The bottom three bits of a capcode are carried by *position*, not by the
     * codeword, so every residue has to be exercised or the frame arithmetic
     * can be wrong in seven cases out of eight and still look fine. */
    for(uint32_t base = 0; base < 8; base++) {
        uint32_t ric = 0x1E2440u + base;
        const PhmGenPage pages[] = {
            {.ric = ric, .func = 3, .numeric = false, .text = "FRAME"},
        };

        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "frame %lu: %u pages", (unsigned long)base, caught.count);
        if(caught.count) {
            CHECK(caught.page[0].ric == ric, "frame %lu: ric %lu, want %lu",
                  (unsigned long)base, (unsigned long)caught.page[0].ric, (unsigned long)ric);
        }
    }

    /* The extremes of the 21-bit address space. */
    const uint32_t edges[] = {8, 0x1FFFFFu, 0x100000u, 0xFFFFFu, 1000000u};
    for(uint8_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        const PhmGenPage pages[] = {
            {.ric = edges[i], .func = 2, .numeric = false, .text = "EDGE"},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "edge %lu: %u pages", (unsigned long)edges[i], caught.count);
        if(caught.count) {
            CHECK(caught.page[0].ric == edges[i], "edge ric %lu, want %lu",
                  (unsigned long)caught.page[0].ric, (unsigned long)edges[i]);
        }
    }
}

static void test_alphabets(void) {
    printf("\nAlphabets and function bits\n");

    /* Numeric: four bits a digit, and the function bits say so. */
    {
        const PhmGenPage pages[] = {
            {.ric = 200001, .func = 0, .numeric = true, .text = "5550142"},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "numeric page count %u", caught.count);
        if(caught.count) {
            CHECK(caught.page[0].kind == PhmPageNumeric, "numeric kind %u", caught.page[0].kind);
            CHECK(strncmp(caught.page[0].text, "5550142", 7) == 0, "numeric text \"%s\"",
                  caught.page[0].text);
        }
    }

    /* Tone only: an address and nothing else. The pager beeped; that is all
     * anyone was told, and all a listener learns. */
    {
        const PhmGenPage pages[] = {
            {.ric = 555123, .func = 1, .numeric = false, .text = NULL},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "tone page count %u", caught.count);
        if(caught.count) {
            CHECK(caught.page[0].kind == PhmPageTone, "tone kind %u", caught.page[0].kind);
            CHECK(caught.page[0].len == 0, "tone text \"%s\"", caught.page[0].text);
            CHECK(caught.page[0].payload_bits == 0, "tone payload %u bits",
                  caught.page[0].payload_bits);
        }
    }

    /* Function 1 and 2 are site-specific, so readable text has to win the tie
     * on its own merits rather than on the function bits. */
    {
        const PhmGenPage pages[] = {
            {.ric = 300002, .func = 1, .numeric = false, .text = "READABLE TEXT HERE"},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "func1 page count %u", caught.count);
        if(caught.count) {
            CHECK(caught.page[0].kind == PhmPageAlpha, "func1 kind %u", caught.page[0].kind);
            CHECK_STR(caught.page[0].text, "READABLE TEXT HERE");
        }
    }

    /* Re-rendering under the other alphabet is offered in the UI because
     * terminals lie about the function bits; it must at least be reversible. */
    {
        const PhmGenPage pages[] = {
            {.ric = 300010, .func = 3, .numeric = false, .text = "ABC"},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        if(caught.count) {
            char buf[PHM_TEXT_MAX];
            uint8_t len = phm_page_render(&caught.page[0], PhmPageAlpha, buf, sizeof(buf));
            CHECK(len == 3, "re-render alpha len %u", len);
            CHECK_STR(buf, "ABC");
            len = phm_page_render(&caught.page[0], PhmPageNumeric, buf, sizeof(buf));
            CHECK(len > 0, "re-render numeric produced nothing");
            len = phm_page_render(&caught.page[0], PhmPageTone, buf, sizeof(buf));
            CHECK(len == 0, "tone render produced %u chars", len);
        }
    }

    /* Every character the alphanumeric alphabet can carry. */
    {
        char wide[64];
        uint8_t n = 0;
        for(char c = ' '; c < 0x7F && n < 60; c++) wide[n++] = c;
        wide[n] = '\0';

        const PhmGenPage pages[] = {
            {.ric = 400001, .func = 3, .numeric = false, .text = wide},
        };
        Caught caught;
        run(pages, 1, 1, 0, &caught, NULL);
        CHECK(caught.count == 1, "wide page count %u", caught.count);
        if(caught.count) {
            /* Trailing spaces are stripped on the way out, deliberately. */
            CHECK(strncmp(caught.page[0].text, wide + 1, 50) == 0 ||
                      strncmp(caught.page[0].text, wide, 50) == 0,
                  "wide charset came back as \"%s\"", caught.page[0].text);
        }
    }
}

static void test_multi_page(void) {
    printf("\nA channel with traffic on it\n");

    uint8_t count = 0;
    const PhmGenPage* demo = phm_gen_demo_pages(&count);

    Caught caught;
    PhmPocsagStatus status;
    uint8_t n = run(demo, count, 1, 0, &caught, &status);

    CHECK(n == count, "caught %u of %u demo pages", n, count);
    CHECK(status.bad_words == 0, "%lu bad words on a clean channel",
          (unsigned long)status.bad_words);
    CHECK(status.orphans == 0, "%lu orphan message words", (unsigned long)status.orphans);
    CHECK(status.batches > 0, "no batches counted");

    for(uint8_t i = 0; i < n && i < count; i++) {
        CHECK(caught.page[i].ric == demo[i].ric, "page %u ric %lu, want %lu", i,
              (unsigned long)caught.page[i].ric, (unsigned long)demo[i].ric);
        if(demo[i].text && !demo[i].numeric) {
            CHECK_STR(caught.page[i].text, demo[i].text);
        }
    }
}

/* --------------------------------------------------------------- damage ---- */

static void test_damage(void) {
    printf("\nWhat the error correction is worth\n");

    const PhmGenPage pages[] = {
        {.ric = 1234567, .func = 3, .numeric = false, .text = "CRASH CALL WARD 4B BED 12"},
    };

    /* One bit in every hundred is one error per three codewords: the code
     * should repair all of it and the text should be perfect. */
    {
        Caught caught;
        PhmPocsagStatus status;
        run(pages, 1, 1, 100, &caught, &status);
        CHECK(caught.count == 1, "light damage: %u pages", caught.count);
        if(caught.count) {
            CHECK_STR(caught.page[0].text, "CRASH CALL WARD 4B BED 12");
            CHECK(caught.page[0].bad_words == 0, "light damage: %u words lost",
                  caught.page[0].bad_words);
        }
        CHECK(status.corrected > 0, "light damage corrected nothing - was any injected?");
    }

    /* One bit in forty averages well under two errors per codeword, which is
     * exactly what BCH(31,21) is specified to survive. */
    {
        Caught caught;
        PhmPocsagStatus status;
        run(pages, 1, 1, 40, &caught, &status);
        CHECK(caught.count == 1, "heavy damage: %u pages", caught.count);
        if(caught.count) {
            CHECK_STR(caught.page[0].text, "CRASH CALL WARD 4B BED 12");
            CHECK(caught.page[0].errors > 0, "heavy damage repaired nothing");
        }
        printf("  1 bit in 40: %lu repaired, %lu words lost\n",
               (unsigned long)status.corrected, (unsigned long)status.bad_words);
    }

    /*
     * The property that actually matters, swept across a hundred different
     * scatterings of heavy damage: a page whose text did not survive must never
     * be handed over looking clean. Either the text is right, or the page
     * carries a count of what was repaired or lost. Silence is an acceptable
     * answer. A confident wrong answer is not - this is a tool for telling
     * people what leaked, and inventing a leak is as bad as missing one.
     */
    {
        uint32_t heard = 0, exact = 0, flagged = 0, silent = 0, lost_words = 0;

        for(uint32_t seed = 0; seed < 100; seed++) {
            Caught caught;
            PhmPocsagStatus status;
            run_seeded(pages, 1, 1, 11, seed, &caught, &status);

            if(status.bad_words) lost_words++;
            if(caught.count == 0) {
                silent++;
                continue;
            }

            for(uint8_t i = 0; i < caught.count; i++) {
                heard++;
                const PhmPage* page = &caught.page[i];
                bool text_ok = strcmp(page->text, "CRASH CALL WARD 4B BED 12") == 0;
                bool ric_ok = page->ric == 1234567u;
                bool declares_damage = page->errors > 0 || page->bad_words > 0 || page->truncated;

                if(text_ok && ric_ok) {
                    exact++;
                } else {
                    flagged += declares_damage ? 1u : 0u;
                    CHECK(declares_damage,
                          "seed %lu: page came back as \"%s\" (ric %lu) claiming to be clean",
                          (unsigned long)seed, page->text, (unsigned long)page->ric);
                }
            }
        }

        CHECK(silent + heard == 100u || heard > 0, "no run produced any outcome at all");
        CHECK(lost_words > 0, "heavy damage never destroyed a codeword");
        printf("  1 bit in 11, 100 scatterings: %lu heard (%lu exact, %lu damaged and "
               "declared), %lu silent\n",
               (unsigned long)heard, (unsigned long)exact, (unsigned long)flagged,
               (unsigned long)silent);
    }
}

static void test_jitter(void) {
    printf("\nClock disagreement\n");

    /* A real slicer's run lengths are never exact multiples of the bit period.
     * Rounding to the nearest bit has to hold up to a fifth of a bit of jitter
     * on every edge, at every rate. */
    for(uint8_t baud = 0; baud < PHM_BAUD_COUNT; baud++) {
        int32_t jitter = phm_bauds[baud].bit_us / 5;

        const PhmGenPage pages[] = {
            {.ric = 1234567, .func = 3, .numeric = false, .text = "JITTER HOLDS"},
        };

        Caught caught;
        memset(&caught, 0, sizeof(caught));
        PhmPocsag* pocsag = phm_pocsag_alloc();
        phm_pocsag_set_callback(pocsag, on_page, &caught);

        JitterFeeder j = {
            .feeder = {.pocsag = pocsag, .pairs = 0, .total_us = 0},
            .jitter_us = jitter,
            .seed = 12345u + baud,
        };
        phm_gen_transmission(pages, 1, baud, 0, 0, on_pair_jitter, &j);
        phm_pocsag_flush(pocsag);
        phm_pocsag_free(pocsag);

        CHECK(caught.count == 1, "%s bps +-%ldus: %u pages", phm_bauds[baud].label,
              (long)jitter, caught.count);
        if(caught.count) CHECK_STR(caught.page[0].text, "JITTER HOLDS");
    }
}

static void test_noise(void) {
    printf("\nAn empty channel\n");

    /*
     * With no transmitter the CC1101's slicer emits noise, and the decoder must
     * find nothing in it. This is the false-positive floor: a privacy tool that
     * invents pages is worse than useless.
     */
    PhmPocsag* pocsag = phm_pocsag_alloc();
    Caught caught;
    memset(&caught, 0, sizeof(caught));
    phm_pocsag_set_callback(pocsag, on_page, &caught);

    uint32_t seed = 987654321u;
    for(uint32_t i = 0; i < 400000u; i++) {
        uint32_t d = 60u + (rnd(&seed) % 3000u);
        phm_pocsag_feed_pair(pocsag, (rnd(&seed) & 1u) != 0u, d);
    }
    phm_pocsag_flush(pocsag);

    PhmPocsagStatus status;
    phm_pocsag_status(pocsag, &status);
    phm_pocsag_free(pocsag);

    CHECK(caught.count == 0, "%u pages hallucinated out of noise", caught.count);
    printf("  400k noise runs: %lu sync hits, %lu pages\n", (unsigned long)status.batches,
           (unsigned long)status.pages);
}

/* A whole transmission, held so it can be replayed from the middle. */
#define MAX_RUNS 16384
static uint32_t recorded_us[MAX_RUNS];
static uint8_t recorded_level[MAX_RUNS];
static uint32_t recorded_count;

static void on_pair_record(void* ctx, bool level, uint32_t duration_us) {
    (void)ctx;
    if(recorded_count >= MAX_RUNS) return;
    recorded_us[recorded_count] = duration_us;
    recorded_level[recorded_count] = level ? 1u : 0u;
    recorded_count++;
}

static void test_partial(void) {
    printf("\nWalking in on a transmission already in progress\n");

    /*
     * Tune in halfway through and the address codeword is already gone. The
     * message words that follow belong to somebody, but there is no way to know
     * who - so they must be counted as orphans and thrown away, not pinned on
     * whichever pager happens to be addressed next.
     */
    const PhmGenPage pages[] = {
        {.ric = 1234567, .func = 3, .numeric = false, .text = "A LONG MESSAGE THAT SPANS WORDS"},
    };

    recorded_count = 0;
    phm_gen_transmission(pages, 1, 1, 0, 0, on_pair_record, NULL);
    CHECK(recorded_count > 40, "collector saw %lu runs", (unsigned long)recorded_count);

    Caught caught;
    memset(&caught, 0, sizeof(caught));
    PhmPocsag* pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(pocsag, on_page, &caught);

    /*
     * Start after the preamble (576 bits), the first sync (32) and all sixteen
     * codewords of the first batch (512) - so the address codeword is behind us
     * and the only thing left is a message with no owner.
     */
    const uint32_t skip_us = (576u + 32u + 512u) * phm_bauds[1].bit_us;
    uint32_t elapsed = 0;
    uint32_t fed = 0;
    for(uint32_t i = 0; i < recorded_count; i++) {
        elapsed += recorded_us[i];
        if(elapsed < skip_us) continue;
        phm_pocsag_feed_pair(pocsag, recorded_level[i] != 0, recorded_us[i]);
        fed++;
    }
    CHECK(fed > 10, "only %lu runs left after the skip", (unsigned long)fed);
    phm_pocsag_flush(pocsag);

    PhmPocsagStatus status;
    phm_pocsag_status(pocsag, &status);
    phm_pocsag_free(pocsag);

    CHECK(caught.count == 0, "attributed %u pages to a capcode it never saw", caught.count);
    CHECK(status.orphans > 0, "no orphan words counted from a half-heard message");
    printf("  half a transmission: %lu orphan words, %u pages\n", (unsigned long)status.orphans,
           caught.count);
}

int main(void) {
    test_bch();
    test_roundtrip();
    test_capcodes();
    test_alphabets();
    test_multi_page();
    test_damage();
    test_jitter();
    test_noise();
    test_partial();
    return phm_report("\npocsag");
}
