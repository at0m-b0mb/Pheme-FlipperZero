/*
 * The session memory, on the host.
 *
 * The ring buffer has to hand pages back newest-first and never hand back a
 * slot it has not written, the roster has to keep one entry per capcode and
 * survive more capcodes than it has room for, and the whole thing is driven
 * here by the real demo channel through the real decoder - so what the tests
 * see is what the Pager Log screen will show.
 */
#include "../helpers/phm_gen.h"
#include "../helpers/phm_pocsag.h"
#include "../helpers/phm_privacy.h"
#include "../helpers/phm_roster.h"
#include "phm_test.h"

#include <string.h>

static PhmRecord make(uint32_t ric, const char* text, uint8_t kind, uint32_t tick) {
    PhmRecord record;
    memset(&record, 0, sizeof(record));
    record.page.ric = ric;
    record.page.kind = kind;
    record.tick_ms = tick;
    if(text) {
        record.page.len = (uint8_t)strlen(text);
        memcpy(record.page.text, text, record.page.len);
    }
    phm_privacy_classify(record.page.text, record.page.len, &record.exposure);
    return record;
}

/* ----------------------------------------------------------------- ring ---- */

static void test_spool(void) {
    printf("The page ring\n");

    PhmSpool spool;
    phm_spool_reset(&spool);

    CHECK(phm_spool_at(&spool, 0) == NULL, "empty ring returned a page");
    CHECK(spool.count == 0, "empty ring count %u", spool.count);

    /* Partly filled: newest first, and nothing past the end. */
    for(uint32_t i = 0; i < 5; i++) {
        PhmRecord record = make(1000 + i, "ROUTINE", PhmPageAlpha, i * 1000u);
        phm_spool_add(&spool, &record);
    }
    CHECK(spool.count == 5, "count %u after five pages", spool.count);
    for(uint8_t i = 0; i < 5; i++) {
        const PhmRecord* got = phm_spool_at(&spool, i);
        CHECK(got != NULL, "slot %u missing", i);
        if(got) {
            CHECK(got->page.ric == 1004u - i, "slot %u holds ric %lu", i,
                  (unsigned long)got->page.ric);
        }
    }
    CHECK(phm_spool_at(&spool, 5) == NULL, "read past the end returned a page");

    /* Overfilled: the oldest fall out, the newest stay in order. */
    for(uint32_t i = 5; i < 5 + PHM_SPOOL_MAX + 3u; i++) {
        PhmRecord record = make(1000 + i, "ROUTINE", PhmPageAlpha, i * 1000u);
        phm_spool_add(&spool, &record);
    }
    CHECK(spool.count == PHM_SPOOL_MAX, "count %u after overflowing", spool.count);
    CHECK(spool.total == 5 + PHM_SPOOL_MAX + 3u, "total %u", spool.total);

    uint32_t newest = 1000u + 5u + PHM_SPOOL_MAX + 2u;
    for(uint8_t i = 0; i < PHM_SPOOL_MAX; i++) {
        const PhmRecord* got = phm_spool_at(&spool, i);
        CHECK(got != NULL, "slot %u missing after overflow", i);
        if(got) {
            CHECK(got->page.ric == newest - i, "slot %u holds ric %lu, want %lu", i,
                  (unsigned long)got->page.ric, (unsigned long)(newest - i));
        }
    }
    CHECK(phm_spool_at(&spool, PHM_SPOOL_MAX) == NULL, "read past a full ring");
}

static void test_worst_survives(void) {
    printf("\nThe worst page of the session is not forgotten\n");

    /*
     * The ring is small and a busy channel scrolls it in a minute. The single
     * most exposed page has to be kept whatever happens, because it is the one
     * the report is about.
     */
    PhmSpool spool;
    phm_spool_reset(&spool);

    PhmRecord bad =
        make(1234567, "MR A SAMPLE WARD 4B DOB 01/01/60 NHS 4990000000", PhmPageAlpha, 0);
    phm_spool_add(&spool, &bad);
    CHECK(spool.have_worst, "worst page not recorded");
    CHECK(spool.worst.exposure.grade == PhmGradeF, "worst page graded %s",
          phm_grade_name(spool.worst.exposure.grade));

    for(uint32_t i = 0; i < PHM_SPOOL_MAX * 3u; i++) {
        PhmRecord dull = make(2000 + i, "OK", PhmPageAlpha, (i + 1) * 1000u);
        phm_spool_add(&spool, &dull);
    }

    CHECK(spool.worst.page.ric == 1234567u, "worst page was overwritten");
    CHECK(spool.worst.exposure.grade == PhmGradeF, "worst grade drifted to %s",
          phm_grade_name(spool.worst.exposure.grade));
    for(uint8_t i = 0; i < spool.count; i++) {
        const PhmRecord* got = phm_spool_at(&spool, i);
        CHECK(!got || got->page.ric != 1234567u, "the bad page is somehow still in the ring");
    }
}

/* --------------------------------------------------------------- roster ---- */

static void test_roster(void) {
    printf("\nOne entry per capcode\n");

    PhmRoster roster;
    phm_roster_reset(&roster);

    /* The same pager, five times, over four minutes. */
    const char* pages[] = {
        "WARD 4B BED 12 CRASH CALL", "RESUS TEAM TO ITU",   "MR A SAMPLE FOR THEATRE",
        "BED 9 TRANSFER",            "CARDIAC ARREST WARD 2",
    };
    for(uint8_t i = 0; i < 5; i++) {
        PhmRecord record = make(1234567, pages[i], PhmPageAlpha, (uint32_t)i * 60000u);
        phm_roster_add(&roster, &record);
    }

    CHECK(roster.count == 1, "five pages to one capcode made %u entries", roster.count);
    const PhmPager* pager = phm_roster_at(&roster, 0);
    CHECK(pager != NULL, "no entry");
    if(pager) {
        CHECK(pager->pages == 5, "pages %u", pager->pages);
        CHECK(pager->first_ms == 0, "first_ms %lu", (unsigned long)pager->first_ms);
        CHECK(pager->last_ms == 240000u, "last_ms %lu", (unsigned long)pager->last_ms);
        CHECK(pager->role == PhmRoleClinical, "role %s", phm_role_name(pager->role));
        CHECK(pager->named == 1, "named %u", pager->named);
        CHECK(pager->located == 3, "located %u", pager->located);

        /*
         * This is the point of keeping a roster at all.
         *
         * Not one of these five pages is worse than a D. The page that names
         * somebody does not say where they are, and the pages that give a ward
         * do not say who is on it - so page by page, the grading engine never
         * reaches its tracking floor and never calls any of them an F.
         *
         * The capcode does what no single page did. Follow it for four minutes
         * and the union of its flags is a name, a location and a medical
         * context, which is a description of one person's afternoon assembled
         * out of fragments that each looked survivable on their own. Nobody
         * transmitted that profile. A listener built it for free.
         */
        CHECK(pager->worst_grade == PhmGradeD, "worst single page graded %s, want D",
              phm_grade_name(pager->worst_grade));
        CHECK((pager->flags & (1u << PhmLeakName)) && (pager->flags & (1u << PhmLeakRoom)) &&
                  (pager->flags & (1u << PhmLeakClinical)),
              "the union of five pages lost something");
    }
}

static void test_roles(void) {
    printf("\nRole inference\n");

    struct {
        const char* text;
        uint8_t kind;
        uint8_t want;
    } cases[] = {
        {NULL, PhmPageTone, PhmRoleTone},
        {"5550142", PhmPageNumeric, PhmRoleCallback},
        {"CARDIAC ARREST WARD 4B", PhmPageAlpha, PhmRoleClinical},
        {"FIRE ALARM ZONE 3 INVESTIGATE", PhmPageAlpha, PhmRoleDispatch},
        {"STOCK DELIVERY SIGN FOR PALLETS", PhmPageAlpha, PhmRoleAdmin},
    };

    for(uint8_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PhmRoster roster;
        phm_roster_reset(&roster);
        for(uint8_t k = 0; k < 3; k++) {
            PhmRecord record = make(500000 + i, cases[i].text, cases[i].kind, k * 1000u);
            phm_roster_add(&roster, &record);
        }
        const PhmPager* pager = phm_roster_at(&roster, 0);
        CHECK(pager && pager->role == cases[i].want, "\"%s\" inferred as %s, want %s",
              cases[i].text ? cases[i].text : "(tone)",
              pager ? phm_role_name(pager->role) : "-", phm_role_name(cases[i].want));
    }

    /* A tone-only pager that later carries text stops being a tone pager. */
    {
        PhmRoster roster;
        phm_roster_reset(&roster);
        PhmRecord tone = make(600001, NULL, PhmPageTone, 0);
        phm_roster_add(&roster, &tone);
        CHECK(phm_roster_at(&roster, 0)->role == PhmRoleTone, "not tone at first");
        PhmRecord text = make(600001, "CARDIAC ARREST", PhmPageAlpha, 1000);
        phm_roster_add(&roster, &text);
        CHECK(phm_roster_at(&roster, 0)->role == PhmRoleClinical, "role did not move on");
    }
}

static void test_overflow(void) {
    printf("\nMore capcodes than the table holds\n");

    PhmRoster roster;
    phm_roster_reset(&roster);

    for(uint32_t i = 0; i < PHM_ROSTER_MAX + 10u; i++) {
        PhmRecord record = make(700000 + i, "ROUTINE", PhmPageAlpha, i * 100u);
        phm_roster_add(&roster, &record);
    }

    CHECK(roster.count == PHM_ROSTER_MAX, "count %u", roster.count);
    CHECK(roster.overflow == 10, "overflow %u, want 10", roster.overflow);

    /* A capcode already in the table must still be counted after the table is
     * full, or a busy pager stops being tracked exactly when it matters. */
    PhmRecord again = make(700000, "ROUTINE AGAIN", PhmPageAlpha, 999999u);
    phm_roster_add(&roster, &again);
    CHECK(roster.item[0].pages == 2, "known capcode not updated once full: %u",
          roster.item[0].pages);
    CHECK(roster.overflow == 10, "known capcode counted as overflow");
}

static void test_sort(void) {
    printf("\nBusiest first\n");

    PhmRoster roster;
    phm_roster_reset(&roster);

    const uint8_t counts[] = {2, 9, 1, 5, 5};
    for(uint8_t i = 0; i < 5; i++) {
        for(uint8_t k = 0; k < counts[i]; k++) {
            PhmRecord record = make(800000 + i, "ROUTINE", PhmPageAlpha, k * 100u);
            phm_roster_add(&roster, &record);
        }
    }

    const PhmPager* busiest = phm_roster_busiest(&roster);
    CHECK(busiest && busiest->ric == 800001u, "busiest is %lu",
          busiest ? (unsigned long)busiest->ric : 0ul);

    phm_roster_sort(&roster);
    for(uint8_t i = 1; i < roster.count; i++) {
        CHECK(roster.item[i - 1].pages >= roster.item[i].pages, "sort broken at %u", i);
    }
    CHECK(roster.item[0].ric == 800001u, "sorted head is %lu", (unsigned long)roster.item[0].ric);
    CHECK(roster.count == 5, "sort changed the count to %u", roster.count);
}

/* ------------------------------------------------------------ the channel -- */

typedef struct {
    PhmRoster roster;
    PhmSpool spool;
    uint32_t clock;
} Session;

static void on_page(void* ctx, const PhmPage* page) {
    Session* session = ctx;

    PhmRecord record;
    memset(&record, 0, sizeof(record));
    record.page = *page;
    record.tick_ms = session->clock;
    record.frequency = 439987500u;
    record.rssi = -74;
    phm_privacy_classify(page->text, page->len, &record.exposure);

    phm_spool_add(&session->spool, &record);
    phm_roster_add(&session->roster, &record);
    session->clock += 30000u;
}

typedef struct {
    PhmPocsag* pocsag;
} Feeder;

static void on_pair(void* ctx, bool level, uint32_t duration_us) {
    phm_pocsag_feed_pair(((Feeder*)ctx)->pocsag, level, duration_us);
}

static void test_live_session(void) {
    printf("\nTen minutes on the demo channel\n");

    Session session;
    memset(&session, 0, sizeof(session));
    phm_roster_reset(&session.roster);
    phm_spool_reset(&session.spool);

    PhmPocsag* pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(pocsag, on_page, &session);

    uint8_t count = 0;
    const PhmGenPage* demo = phm_gen_demo_pages(&count);
    Feeder feeder = {.pocsag = pocsag};

    /* Four passes of the demo channel, as if it had been left running. */
    for(uint8_t pass = 0; pass < 4; pass++) {
        phm_gen_transmission(demo, count, 1, 0, pass, on_pair, &feeder);
    }
    phm_pocsag_flush(pocsag);
    phm_pocsag_free(pocsag);

    PhmTally tally;
    phm_tally(&session.roster, &session.spool, &tally);

    CHECK(tally.pages == (uint16_t)count * 4u, "heard %u pages, want %u", tally.pages, count * 4);
    CHECK(tally.capcodes == 5, "counted %u distinct capcodes, want 5", tally.capcodes);
    CHECK(tally.worst_grade == PhmGradeF, "worst grade %s", phm_grade_name(tally.worst_grade));
    CHECK(tally.named_pages > 0, "no page named anybody");
    CHECK(tally.located_pages > 0, "no page located anybody");
    CHECK(session.roster.overflow == 0, "roster overflowed on five capcodes");

    const PhmPager* busiest = phm_roster_busiest(&session.roster);
    CHECK(busiest != NULL, "no busiest capcode");
    if(busiest) {
        /* The line the app exists to be able to print. */
        uint32_t minutes = (busiest->last_ms - busiest->first_ms) / 60000u;
        printf("  pager %lu: %u pages over %lu min, %u named a person, %u gave a "
               "location, worst %s (%s)\n",
               (unsigned long)busiest->ric, busiest->pages, (unsigned long)minutes,
               busiest->named, busiest->located, phm_grade_name(busiest->worst_grade),
               phm_role_name(busiest->role));

        CHECK(busiest->ric == 1234567u, "busiest is %lu", (unsigned long)busiest->ric);
        CHECK(busiest->pages == 12, "busiest heard %u times", busiest->pages);
        CHECK(busiest->role == PhmRoleClinical, "busiest role %s", phm_role_name(busiest->role));
        CHECK(busiest->last_ms > busiest->first_ms, "no time passed");
    }

    phm_roster_sort(&session.roster);
    for(uint8_t i = 0; i < session.roster.count; i++) {
        const PhmPager* pager = phm_roster_at(&session.roster, i);
        printf("    %-8lu %2u pages  %-9s  worst %s\n", (unsigned long)pager->ric, pager->pages,
               phm_role_name(pager->role), phm_grade_name(pager->worst_grade));
    }
}

int main(void) {
    test_spool();
    test_worst_survives();
    test_roster();
    test_roles();
    test_overflow();
    test_sort();
    test_live_session();
    return phm_report("\nroster");
}
