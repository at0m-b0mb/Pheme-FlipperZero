/*
 * The grading engine, on the host.
 *
 * Two kinds of check live here. The first is ordinary: does each detector fire
 * on the thing it is for, and stay quiet otherwise. The second is the set of
 * promises the app makes to the person reading it - that no page can be graded
 * better than B, that a named person plus a location is always an F, that
 * adding a leak never improves a grade, and that a redacted message really has
 * had the leak taken out of it. Those are properties, so they are swept rather
 * than sampled.
 */
#include "../helpers/phm_gen.h"
#include "../helpers/phm_pocsag.h"
#include "../helpers/phm_privacy.h"
#include "phm_test.h"

#include <string.h>

static PhmExposure classify(const char* text) {
    PhmExposure exposure;
    phm_privacy_classify(text, (uint8_t)strlen(text), &exposure);
    return exposure;
}

static bool has(const PhmExposure* e, uint8_t leak) {
    return (e->flags & (1u << leak)) != 0u;
}

/* The text a span actually covers, for checking that spans land where they
 * should rather than merely existing. */
static bool covers(const PhmExposure* e, const char* text, const char* want, uint8_t leak) {
    for(uint8_t i = 0; i < e->n_spans; i++) {
        if(e->span[i].leak != leak) continue;
        if(e->span[i].len != (uint8_t)strlen(want)) continue;
        if(strncmp(text + e->span[i].start, want, e->span[i].len) == 0) return true;
    }
    return false;
}

/* ----------------------------------------------------------- detectors ---- */

static void test_detectors(void) {
    printf("Detectors\n");

    struct {
        const char* text;
        uint8_t leak;
        const char* span;
    } positives[] = {
        {"WARD 4B BED 12 MR A SAMPLE ATTEND", PhmLeakName, "MR A SAMPLE"},
        {"CALL DR PATEL URGENTLY", PhmLeakName, "DR PATEL"},
        {"PT J DOE FOR THEATRE", PhmLeakName, "PT J DOE"},
        {"SISTER OKAFOR TO CALL BACK", PhmLeakName, "SISTER OKAFOR"},
        {"DOB 01/01/60 CONFIRMED", PhmLeakDob, "DOB 01/01/60"},
        {"ADMITTED 14/02/1978 VIA ED", PhmLeakDob, "14/02/1978"},
        {"NHS 4990000000 TRANSFER", PhmLeakPatientId, "NHS 4990000000"},
        {"MRN 883421 TO RADIOLOGY", PhmLeakPatientId, "MRN 883421"},
        {"MAIL OPS@EXAMPLE.COM FOR ROTA", PhmLeakEmail, "OPS@EXAMPLE.COM"},
        {"CALL 5550142 NOW", PhmLeakPhone, "5550142"},
        {"RING 020 7946 0018 ASAP", PhmLeakPhone, "020 7946 0018"},
        {"BLEEP 4471 WHEN FREE", PhmLeakExtension, "BLEEP 4471"},
        {"CALL EXT 2210 SWITCHBOARD", PhmLeakExtension, "EXT 2210"},
        {"ATTEND 14 ELM ROAD", PhmLeakAddress, "14 ELM ROAD"},
        {"RTC AT 221 BAKER STREET", PhmLeakAddress, "221 BAKER STREET"},
        {"DELIVER TO SW1A 1AA GATEHOUSE", PhmLeakPostcode, "SW1A 1AA"},
        {"WARD 4B PLEASE", PhmLeakRoom, "WARD 4B"},
        {"MOVE TO BAY 2", PhmLeakRoom, "BAY 2"},
        {"FIRE PANEL ZONE 3", PhmLeakRoom, "ZONE 3"},
        {"DOOR CODE 4471 SIDE ENTRANCE", PhmLeakCredential, "DOOR CODE 4471"},
        {"PIN 9932 FOR THE SAFE", PhmLeakCredential, "PIN 9932"},
        {"JOB 774512 ASSIGNED", PhmLeakLongNumber, "JOB 774512"},
    };

    for(uint8_t i = 0; i < sizeof(positives) / sizeof(positives[0]); i++) {
        PhmExposure e = classify(positives[i].text);
        CHECK(has(&e, positives[i].leak), "\"%s\": %s not detected", positives[i].text,
              phm_leak_short(positives[i].leak));
        CHECK(covers(&e, positives[i].text, positives[i].span, positives[i].leak),
              "\"%s\": span for %s did not cover \"%s\"", positives[i].text,
              phm_leak_short(positives[i].leak), positives[i].span);
    }

    /* Vocabulary sets a flag but deliberately claims no span: blanking out the
     * word "CARDIAC" leaves a message that no longer says why it was sent. */
    {
        PhmExposure e = classify("CARDIAC ARREST RESUS TEAM ATTEND");
        CHECK(has(&e, PhmLeakClinical), "clinical vocabulary missed");
        CHECK(e.n_spans == 0, "clinical vocabulary claimed %u spans", e.n_spans);
    }
    {
        PhmExposure e = classify("FIRE ALARM ACTIVATION INVESTIGATE");
        CHECK(has(&e, PhmLeakIncident), "incident vocabulary missed");
    }
}

static void test_negatives(void) {
    printf("\nThings that are not leaks\n");

    struct {
        const char* text;
        uint8_t leak;
    } negatives[] = {
        /* A medical emergency code is not a door code. */
        {"CODE BLUE MAIN THEATRE", PhmLeakCredential},
        /* A title with no name after it names nobody. */
        {"DR REQUIRED ON WARD", PhmLeakName},
        {"PT 4471 ADMITTED", PhmLeakName},
        /* Short numbers are not telephone numbers. */
        {"BAY 2 TROLLEY 14", PhmLeakPhone},
        {"ZONE 3 LEVEL 2", PhmLeakPhone},
        /* A hospital number stays a hospital number. */
        {"NHS 4990000000 TRANSFER", PhmLeakPhone},
        /* A room needs a number to be a room. */
        {"ROOM AVAILABLE PLEASE CONFIRM", PhmLeakRoom},
        /* No street suffix, no address. */
        {"14 UNITS DELIVERED", PhmLeakAddress},
        {"TEST MESSAGE PLEASE IGNORE", PhmLeakName},
        {"SYSTEM TEST 1 OF 3", PhmLeakPhone},
    };

    for(uint8_t i = 0; i < sizeof(negatives) / sizeof(negatives[0]); i++) {
        PhmExposure e = classify(negatives[i].text);
        CHECK(!has(&e, negatives[i].leak), "\"%s\": falsely reported %s", negatives[i].text,
              phm_leak_short(negatives[i].leak));
    }

    /* A record number claimed by its label must not be counted a second time
     * by the sweep for unlabelled long numbers. */
    {
        PhmExposure e = classify("NHS 4990000000 TRANSFER TO ITU BED 3");
        CHECK(has(&e, PhmLeakPatientId), "patient id missed");
        CHECK(!has(&e, PhmLeakPhone), "hospital number double-counted as a phone number");
        CHECK(!has(&e, PhmLeakLongNumber), "hospital number double-counted as a reference");
    }
}

/* --------------------------------------------------------------- floors ---- */

static void test_floors(void) {
    printf("\nThe floors\n");

    /* A page that says nothing at all. It still went out in clear, addressed to
     * a capcode that belongs to one pager and will belong to it next year too. */
    {
        PhmExposure e;
        phm_privacy_classify(NULL, 0, &e);
        CHECK(e.grade == PhmGradeB, "tone-only page graded %s, want B", phm_grade_name(e.grade));
        CHECK(e.floor == PhmFloorCleartext, "tone-only floor %u", e.floor);
        CHECK(e.n_spans == 0, "tone-only page has spans");
    }

    {
        PhmExposure e = classify("MR A SAMPLE TO CALL");
        CHECK(e.grade >= PhmGradeD, "a named person graded %s, want D or worse",
              phm_grade_name(e.grade));
        CHECK(e.floor == PhmFloorIdentity, "identity floor not applied");
    }

    {
        PhmExposure e = classify("MR A SAMPLE WARD 4B");
        CHECK(e.grade == PhmGradeF, "name plus ward graded %s, want F", phm_grade_name(e.grade));
        CHECK(e.floor == PhmFloorTracking, "tracking floor not applied");
    }

    {
        PhmExposure e = classify("DOOR CODE 4471");
        CHECK(e.grade == PhmGradeF, "a door code graded %s, want F", phm_grade_name(e.grade));
        CHECK(e.floor == PhmFloorSecret, "secret floor not applied");
    }

    {
        PhmExposure e = classify("STOCK DELIVERY BAY 2 SIGN FOR PALLETS");
        CHECK(e.grade == PhmGradeC, "an operational page graded %s, want C",
              phm_grade_name(e.grade));
    }
}

static void test_unreachable_grades(void) {
    printf("\nA+ and A cannot be earned\n");

    /*
     * The claim on the box is that no POCSAG page can be graded better than B.
     * It is a claim about the arithmetic, not about the corpus, so it is checked
     * against every score the engine can produce rather than against examples.
     */
    for(uint16_t raw = 0; raw <= 100; raw++) {
        for(uint8_t floor = PhmFloorCleartext; floor < PhmFloorCount; floor++) {
            /* Mirrors phm_privacy_classify's final step. */
            static const uint8_t floor_score[PhmFloorCount] = {0, 16, 31, 51, 86, 86};
            uint8_t base = floor_score[floor];
            uint8_t score = (uint8_t)(base + ((100u - base) * raw) / 100u);
            uint8_t grade = phm_grade_of_score(score);
            CHECK(grade >= PhmGradeB, "floor %u raw %u produced grade %s", floor, raw,
                  phm_grade_name(grade));
        }
    }

    /* And against real text, including the emptiest page there is. */
    const char* quiet[] = {"", "OK", "TEST", "ACK", "1", "PLEASE CONFIRM", "THANKS"};
    for(uint8_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++) {
        PhmExposure e = classify(quiet[i]);
        CHECK(e.grade >= PhmGradeB, "\"%s\" graded %s", quiet[i], phm_grade_name(e.grade));
    }
}

static void test_monotonic(void) {
    printf("\nAdding a leak never improves the grade\n");

    /* Built up one piece at a time. Each addition must score at least as badly
     * as the one before it - a scoring engine that lets a message get *safer*
     * by naming somebody has a sign error somewhere. */
    const char* ladder[] = {
        "PLEASE CONFIRM",
        "PLEASE CONFIRM BAY 2",
        "PLEASE CONFIRM BAY 2 CARDIAC",
        "PLEASE CONFIRM BAY 2 CARDIAC BLEEP 4471",
        "PLEASE CONFIRM BAY 2 CARDIAC BLEEP 4471 MR A SAMPLE",
        "PLEASE CONFIRM BAY 2 CARDIAC BLEEP 4471 MR A SAMPLE DOB 01/01/60",
        "PLEASE CONFIRM BAY 2 CARDIAC BLEEP 4471 MR A SAMPLE DOB 01/01/60 PIN 9932",
    };

    uint8_t previous = 0;
    for(uint8_t i = 0; i < sizeof(ladder) / sizeof(ladder[0]); i++) {
        PhmExposure e = classify(ladder[i]);
        CHECK(e.score >= previous, "step %u scored %u, down from %u: \"%s\"", i, e.score,
              previous, ladder[i]);
        previous = e.score;
    }
    CHECK(previous >= 86, "the full ladder only reached %u", previous);
}

static void test_curve_not_clip(void) {
    printf("\nFloors compress, they do not clip\n");

    /*
     * Two pages that both hit the tracking floor. If the floor were a clip they
     * would score identically, and the UI would be unable to say which of two
     * F-graded pages gave more away. The one that also carries a date of birth,
     * a hospital number and a callback has to come out higher.
     */
    PhmExposure light = classify("MR A SAMPLE WARD 4B");
    PhmExposure heavy =
        classify("MR A SAMPLE WARD 4B BED 12 DOB 01/01/60 NHS 4990000000 BLEEP 4471 CARDIAC");

    CHECK(light.floor == PhmFloorTracking, "light page floor %u", light.floor);
    CHECK(heavy.floor == PhmFloorTracking, "heavy page floor %u", heavy.floor);
    CHECK(heavy.score > light.score, "clipped: both pages scored %u", light.score);
    CHECK(light.score >= 86, "light page fell below its floor at %u", light.score);
    printf("  same floor, %u vs %u\n", light.score, heavy.score);
}

/* ------------------------------------------------------------- redaction ---- */

static void test_redaction(void) {
    printf("\nRedaction really removes it\n");

    const char* text = "WARD 4B BED 12 MR A SAMPLE DOB 01/01/60 CRASH CALL";
    PhmExposure e = classify(text);

    char out[PHM_TEXT_MAX];
    uint8_t len = phm_privacy_redact(text, (uint8_t)strlen(text), &e, out, sizeof(out));

    CHECK(len == strlen(text), "redaction changed the length: %u vs %zu", len, strlen(text));
    CHECK(strstr(out, "SAMPLE") == NULL, "the name survived redaction: \"%s\"", out);
    CHECK(strstr(out, "01/01/60") == NULL, "the date of birth survived: \"%s\"", out);
    CHECK(strstr(out, "4B") == NULL, "the ward survived: \"%s\"", out);
    /* The words that are not somebody's data stay, so the reader can still see
     * what kind of message it was. */
    CHECK(strstr(out, "CRASH CALL") != NULL, "redaction ate the context: \"%s\"", out);
    printf("  \"%s\"\n", out);

    /* Spans must be inside the message and must not overlap each other, or the
     * view will draw redaction blocks on top of one another. */
    for(uint8_t i = 0; i < e.n_spans; i++) {
        uint16_t end = (uint16_t)e.span[i].start + e.span[i].len;
        CHECK(end <= strlen(text), "span %u runs past the end of the message", i);
        CHECK(e.span[i].len > 0, "span %u is empty", i);
        for(uint8_t j = (uint8_t)(i + 1); j < e.n_spans; j++) {
            uint16_t other_end = (uint16_t)e.span[j].start + e.span[j].len;
            bool overlap = e.span[i].start < other_end && e.span[j].start < end;
            CHECK(!overlap, "spans %u and %u overlap", i, j);
        }
    }

    /* Every character a span claims must report as sensitive, and no character
     * outside one may. */
    for(uint8_t i = 0; i < strlen(text); i++) {
        bool inside = false;
        for(uint8_t s = 0; s < e.n_spans; s++) {
            if(i >= e.span[s].start && i < (uint16_t)e.span[s].start + e.span[s].len) inside = true;
        }
        CHECK(phm_privacy_is_sensitive(&e, i) == inside, "sensitivity disagrees at %u", i);
    }
}

/* ------------------------------------------------------------- the channel -- */

typedef struct {
    PhmPage page[16];
    uint8_t count;
} Caught;

static void on_page(void* ctx, const PhmPage* page) {
    Caught* c = ctx;
    if(c->count < 16) c->page[c->count++] = *page;
}

typedef struct {
    PhmPocsag* pocsag;
} Feeder;

static void on_pair(void* ctx, bool level, uint32_t duration_us) {
    Feeder* f = ctx;
    phm_pocsag_feed_pair(f->pocsag, level, duration_us);
}

static void test_demo_channel(void) {
    printf("\nThe demo channel, graded\n");

    uint8_t count = 0;
    const PhmGenPage* demo = phm_gen_demo_pages(&count);

    Caught caught;
    memset(&caught, 0, sizeof(caught));
    PhmPocsag* pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(pocsag, on_page, &caught);
    Feeder feeder = {.pocsag = pocsag};
    phm_gen_transmission(demo, count, 1, 0, 0, on_pair, &feeder);
    phm_pocsag_flush(pocsag);
    phm_pocsag_free(pocsag);

    CHECK(caught.count == count, "decoded %u of %u demo pages", caught.count, count);

    /* The spread the demo is meant to show: the best a page can do, some
     * ordinary operational traffic, and the two that should stop a reader. */
    const uint8_t want[] = {
        PhmGradeF, /* ward, bed, name, date of birth, crash call */
        PhmGradeC, /* a callback number                          */
        PhmGradeC, /* a bleep and an extension                   */
        PhmGradeD, /* a fire panel and where it is               */
        PhmGradeB, /* tone only - the best any page can do       */
        PhmGradeF, /* a named patient being moved                */
        PhmGradeF, /* a door code and the door it opens          */
        PhmGradeC, /* a delivery                                 */
    };

    uint8_t worst = 0;
    for(uint8_t i = 0; i < caught.count && i < count; i++) {
        PhmExposure e;
        phm_privacy_classify(caught.page[i].text, caught.page[i].len, &e);
        if(e.grade > worst) worst = e.grade;

        printf("  %-8lu %-2s %3u  %s\n", (unsigned long)caught.page[i].ric,
               phm_grade_name(e.grade), e.score,
               caught.page[i].len ? caught.page[i].text : "(tone only)");

        if(i < sizeof(want) / sizeof(want[0])) {
            CHECK(e.grade == want[i], "demo page %u graded %s, want %s", i,
                  phm_grade_name(e.grade), phm_grade_name(want[i]));
        }
    }
    CHECK(worst == PhmGradeF, "the demo channel never reaches F");
}

/* ------------------------------------------------------------------ fuzz ---- */

static uint32_t rnd(uint32_t* seed) {
    *seed = (*seed * 1103515245u) + 12345u;
    return (*seed >> 16) & 0x7FFFu;
}

static void test_fuzz(void) {
    printf("\nAnything a damaged page can produce\n");

    /*
     * A page off the air is not a well-formed sentence. It can be all
     * punctuation, all digits, one character long, or ninety-five characters of
     * whatever the error correction gave up on. The classifier runs on the
     * Sub-GHz worker thread, so it does not get to crash.
     */
    uint32_t seed = 20260812u;
    char buffer[PHM_TEXT_MAX];

    for(uint32_t i = 0; i < 200000u; i++) {
        uint8_t len = (uint8_t)(rnd(&seed) % (PHM_TEXT_MAX - 1));
        for(uint8_t k = 0; k < len; k++) {
            uint32_t r = rnd(&seed) % 5u;
            if(r == 0)
                buffer[k] = (char)('0' + (rnd(&seed) % 10));
            else if(r == 1)
                buffer[k] = (char)('A' + (rnd(&seed) % 26));
            else if(r == 2)
                buffer[k] = ' ';
            else if(r == 3)
                buffer[k] = (char)(0x20 + (rnd(&seed) % 95));
            else
                buffer[k] = "/-.@'+"[rnd(&seed) % 6];
        }
        buffer[len] = '\0';

        PhmExposure e;
        phm_privacy_classify(buffer, len, &e);

        CHECK(e.score >= 16, "fuzz produced score %u", e.score);
        CHECK(e.grade < PhmGradeCount && e.grade >= PhmGradeB, "fuzz produced grade %u", e.grade);
        CHECK(e.n_spans <= PHM_SPAN_MAX, "fuzz produced %u spans", e.n_spans);

        for(uint8_t s = 0; s < e.n_spans; s++) {
            CHECK((uint16_t)e.span[s].start + e.span[s].len <= len,
                  "fuzz span %u runs past the message", s);
            CHECK(e.span[s].leak < PhmLeakCount, "fuzz span has leak %u", e.span[s].leak);
        }

        char out[PHM_TEXT_MAX];
        uint8_t n = phm_privacy_redact(buffer, len, &e, out, sizeof(out));
        CHECK(n == len, "fuzz redaction changed length %u -> %u", len, n);
    }
    printf("  200k random pages classified and redacted\n");
}

int main(void) {
    test_detectors();
    test_negatives();
    test_floors();
    test_unreachable_grades();
    test_monotonic();
    test_curve_not_clip();
    test_redaction();
    test_demo_channel();
    test_fuzz();
    return phm_report("\nprivacy");
}
