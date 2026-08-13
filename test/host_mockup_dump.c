/*
 * Feeds tools_gen_mockups.py.
 *
 * Runs the real generator through the real decoder, classifies the result with
 * the real grading engine, and prints what the screens will contain. The
 * screenshots in the README are drawn from this output, so they cannot drift
 * away from what the app does - if the classifier changes its mind about a
 * page, the picture in the README changes with it.
 *
 * Asserts nothing. It only reports.
 */
#include "../helpers/phm_gen.h"
#include "../helpers/phm_pocsag.h"
#include "../helpers/phm_privacy.h"
#include "../helpers/phm_roster.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    PhmRoster roster;
    PhmSpool spool;
    uint32_t clock;
} Session;

static void on_page(void* context, const PhmPage* page) {
    Session* session = context;

    PhmRecord record;
    memset(&record, 0, sizeof(record));
    record.page = *page;
    record.tick_ms = session->clock;
    record.frequency = 439987500u;
    record.baud_idx = page->baud_idx;
    record.rssi = -74;
    phm_privacy_classify(page->text, page->len, &record.exposure);

    phm_spool_add(&session->spool, &record);
    phm_roster_add(&session->roster, &record);
    session->clock += 47000u;
}

static void on_pair(void* context, bool level, uint32_t duration_us) {
    phm_pocsag_feed_pair((PhmPocsag*)context, level, duration_us);
}

static void dump_record(const PhmRecord* record, uint8_t index) {
    const PhmPage* page = &record->page;
    const PhmExposure* exposure = &record->exposure;

    printf(
        "PAGE index=%u ric=%lu kind=%s func=%u baud=%s grade=%s score=%u "
        "errors=%u bad=%u redacted=%u chars=%u rssi=%d\n",
        index,
        (unsigned long)page->ric,
        phm_page_kind_name(page->kind),
        page->func,
        phm_bauds[record->baud_idx % PHM_BAUD_COUNT].label,
        phm_grade_name(exposure->grade),
        exposure->score,
        page->errors,
        page->bad_words,
        exposure->redacted_chars,
        exposure->chars,
        record->rssi);

    printf("TEXT %s\n", page->len ? page->text : "");
    printf("FLOOR %s\n", phm_floor_reason(exposure->floor));

    /* One character per character of the message: '#' where the classifier
     * found somebody's data, '.' where it did not. This is exactly what the
     * redaction bar draws from. */
    printf("MASK ");
    for(uint8_t i = 0; i < page->len; i++) {
        char c = page->text[i];
        if(c == ' ') {
            printf(" ");
        } else {
            printf("%c", phm_privacy_is_sensitive(exposure, i) ? '#' : '.');
        }
    }
    printf("\n");

    printf("LEAKS ");
    bool first = true;
    for(uint8_t leak = 0; leak < PhmLeakCount; leak++) {
        if(!(exposure->flags & (1u << leak))) continue;
        printf("%s%s", first ? "" : ", ", phm_leak_short(leak));
        first = false;
    }
    if(first) printf("nothing identifiable");
    printf("\n");
}

int main(void) {
    Session session;
    memset(&session, 0, sizeof(session));
    phm_roster_reset(&session.roster);
    phm_spool_reset(&session.spool);

    PhmPocsag* pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(pocsag, on_page, &session);

    uint8_t count = 0;
    const PhmGenPage* demo = phm_gen_demo_pages(&count);

    /* Two passes, so the capcode log has something to show. */
    for(uint8_t pass = 0; pass < 2; pass++) {
        phm_gen_transmission(demo, count, 1, 0, pass, on_pair, pocsag);
    }
    phm_pocsag_flush(pocsag);

    PhmPocsagStatus status;
    phm_pocsag_status(pocsag, &status);
    phm_pocsag_free(pocsag);

    printf(
        "STATUS lock=%u baud=%s batches=%lu words=%lu bad=%lu corrected=%lu\n",
        status.lock,
        (status.baud_idx >= 0) ? phm_bauds[status.baud_idx].label : "?",
        (unsigned long)status.batches,
        (unsigned long)status.words,
        (unsigned long)status.bad_words,
        (unsigned long)status.corrected);

    /* Newest first, exactly as the reader walks them. */
    for(uint8_t i = 0; i < session.spool.count; i++) {
        const PhmRecord* record = phm_spool_at(&session.spool, i);
        if(record) dump_record(record, i);
    }

    PhmTally tally;
    phm_tally(&session.roster, &session.spool, &tally);
    printf(
        "TALLY pages=%u capcodes=%u worst=%s score=%u named=%u located=%u\n",
        tally.pages,
        tally.capcodes,
        phm_grade_name(tally.worst_grade),
        tally.worst_score,
        tally.named_pages,
        tally.located_pages);

    phm_roster_sort(&session.roster);
    for(uint8_t i = 0; i < session.roster.count; i++) {
        const PhmPager* pager = phm_roster_at(&session.roster, i);
        printf(
            "PAGER ric=%lu pages=%u role=%s grade=%s named=%u located=%u minutes=%lu\n",
            (unsigned long)pager->ric,
            pager->pages,
            phm_role_name(pager->role),
            phm_grade_name(pager->worst_grade),
            pager->named,
            pager->located,
            (unsigned long)((pager->last_ms - pager->first_ms) / 60000u));
    }

    return 0;
}
