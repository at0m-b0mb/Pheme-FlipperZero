#include "phm_roster.h"

#include <string.h>

static const char* const phm_role_label[PhmRoleCount] = {
    [PhmRoleUnknown] = "Unknown", [PhmRoleTone] = "Tone only",
    [PhmRoleCallback] = "Callback", [PhmRoleClinical] = "Clinical",
    [PhmRoleDispatch] = "Dispatch", [PhmRoleAdmin] = "Routine",
};

static const char* const phm_role_hint_text[PhmRoleCount] = {
    [PhmRoleUnknown] = "not enough traffic to say",
    [PhmRoleTone] = "beeps, never says why",
    [PhmRoleCallback] = "always a number to ring",
    [PhmRoleClinical] = "carries patients and wards",
    [PhmRoleDispatch] = "carries incidents and responses",
    [PhmRoleAdmin] = "routine site traffic",
};

/* ---------------------------------------------------------------- spool ---- */

void phm_spool_reset(PhmSpool* spool) {
    if(!spool) return;
    memset(spool, 0, sizeof(*spool));
}

void phm_spool_add(PhmSpool* spool, const PhmRecord* record) {
    if(!spool || !record) return;

    spool->item[spool->head] = *record;
    spool->head = (uint8_t)((spool->head + 1u) % PHM_SPOOL_MAX);
    if(spool->count < PHM_SPOOL_MAX) spool->count++;
    if(spool->total < 0xFFFFu) spool->total++;

    if(!spool->have_worst || record->exposure.score > spool->worst.exposure.score) {
        spool->worst = *record;
        spool->have_worst = true;
    }
}

const PhmRecord* phm_spool_at(const PhmSpool* spool, uint8_t index) {
    if(!spool || index >= spool->count) return NULL;
    /* head points at the next slot to write, so head-1 is the newest. */
    uint8_t slot = (uint8_t)((spool->head + PHM_SPOOL_MAX - 1u - index) % PHM_SPOOL_MAX);
    return &spool->item[slot];
}

/* --------------------------------------------------------------- roster ---- */

void phm_roster_reset(PhmRoster* roster) {
    if(!roster) return;
    memset(roster, 0, sizeof(*roster));
}

static const uint16_t phm_identity_mask = (1u << PhmLeakName) | (1u << PhmLeakDob) |
                                          (1u << PhmLeakPatientId) | (1u << PhmLeakEmail);
static const uint16_t phm_location_mask =
    (1u << PhmLeakAddress) | (1u << PhmLeakPostcode) | (1u << PhmLeakRoom);

/*
 * What is this pager for? Answered from what it carries, in the order that a
 * person would answer it: a pager that never says anything cannot be anything
 * else, a pager that only ever sends digits is a callback, and after that the
 * vocabulary decides.
 */
static uint8_t phm_infer_role(const PhmPager* pager) {
    if(pager->pages == 0) return PhmRoleUnknown;

    const uint8_t tone = (uint8_t)(1u << PhmPageTone);
    const uint8_t numeric = (uint8_t)(1u << PhmPageNumeric);

    if(pager->kinds == tone) return PhmRoleTone;
    if((pager->kinds & ~(uint8_t)(tone | numeric)) == 0u) return PhmRoleCallback;
    if(pager->clinical > 0 && pager->clinical >= pager->incident) return PhmRoleClinical;
    if(pager->incident > 0) return PhmRoleDispatch;
    return PhmRoleAdmin;
}

void phm_roster_add(PhmRoster* roster, const PhmRecord* record) {
    if(!roster || !record) return;

    PhmPager* pager = NULL;
    for(uint8_t i = 0; i < roster->count; i++) {
        if(roster->item[i].ric == record->page.ric) {
            pager = &roster->item[i];
            break;
        }
    }

    if(!pager) {
        if(roster->count >= PHM_ROSTER_MAX) {
            if(roster->overflow < 0xFFFFu) roster->overflow++;
            return;
        }
        pager = &roster->item[roster->count++];
        memset(pager, 0, sizeof(*pager));
        pager->ric = record->page.ric;
        pager->first_ms = record->tick_ms;
    }

    if(pager->pages < 0xFFFFu) pager->pages++;
    pager->last_ms = record->tick_ms;
    pager->flags |= record->exposure.flags;
    pager->kinds |= (uint8_t)(1u << (record->page.kind & 3u));

    if(record->exposure.score > pager->worst_score) {
        pager->worst_score = record->exposure.score;
        pager->worst_grade = record->exposure.grade;
    }

    if((record->exposure.flags & (1u << PhmLeakClinical)) && pager->clinical < 255) {
        pager->clinical++;
    }
    if((record->exposure.flags & (1u << PhmLeakIncident)) && pager->incident < 255) {
        pager->incident++;
    }
    if((record->exposure.flags & phm_identity_mask) && pager->named < 255) pager->named++;
    if((record->exposure.flags & phm_location_mask) && pager->located < 255) pager->located++;

    pager->role = phm_infer_role(pager);
}

void phm_roster_sort(PhmRoster* roster) {
    if(!roster || roster->count < 2) return;

    /* Insertion sort: the table is at most thirty-two entries and this runs on
     * a button press, not in the capture path. */
    for(uint8_t i = 1; i < roster->count; i++) {
        PhmPager key = roster->item[i];
        int16_t j = (int16_t)i - 1;
        while(j >= 0) {
            const PhmPager* other = &roster->item[j];
            bool after = (other->pages < key.pages) ||
                         (other->pages == key.pages && other->worst_score < key.worst_score);
            if(!after) break;
            roster->item[j + 1] = roster->item[j];
            j--;
        }
        roster->item[j + 1] = key;
    }
}

const PhmPager* phm_roster_at(const PhmRoster* roster, uint8_t index) {
    if(!roster || index >= roster->count) return NULL;
    return &roster->item[index];
}

const PhmPager* phm_roster_busiest(const PhmRoster* roster) {
    if(!roster || roster->count == 0) return NULL;

    const PhmPager* best = &roster->item[0];
    for(uint8_t i = 1; i < roster->count; i++) {
        if(roster->item[i].pages > best->pages) best = &roster->item[i];
    }
    return best;
}

void phm_tally(const PhmRoster* roster, const PhmSpool* spool, PhmTally* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    if(!roster || !spool) return;

    out->pages = spool->total;
    out->capcodes = roster->count;

    bool first = true;
    for(uint8_t i = 0; i < roster->count; i++) {
        const PhmPager* pager = &roster->item[i];
        out->flags |= pager->flags;
        out->named_pages = (uint16_t)(out->named_pages + pager->named);
        out->located_pages = (uint16_t)(out->located_pages + pager->located);

        if(pager->worst_score > out->worst_score) {
            out->worst_score = pager->worst_score;
            out->worst_grade = pager->worst_grade;
        }

        if(first || pager->first_ms < out->first_ms) out->first_ms = pager->first_ms;
        if(first || pager->last_ms > out->last_ms) out->last_ms = pager->last_ms;
        first = false;
    }
}

const char* phm_role_name(uint8_t role) {
    return (role < PhmRoleCount) ? phm_role_label[role] : "?";
}

const char* phm_role_hint(uint8_t role) {
    return (role < PhmRoleCount) ? phm_role_hint_text[role] : "?";
}
