/*
 * phm_roster - what the session remembers.
 *
 * Two things, because they make two different points.
 *
 * The spool keeps the last few pages so they can be read back. The roster keeps
 * one entry per capcode, and that is the uncomfortable one: a capcode is not a
 * session token or a rotating identifier, it is a number burned into a pager
 * that will still be the same number next year. Sit on a channel for ten
 * minutes and you do not just learn what was said - you learn that pager
 * 1234567 is paged nine times an hour, always with a ward number, and pager
 * 555123 only ever beeps. Nobody consented to that, and nobody can tell it is
 * happening, because listening leaves no trace on the network at all.
 *
 * The role guess is deliberately coarse and deliberately labelled a guess. The
 * point is not that the app can tell a nurse from a caretaker; the point is
 * that a stranger with a thirty-pound radio can start to.
 *
 * Free of furi_ headers - the clock arrives as a parameter.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "phm_pocsag.h"
#include "phm_privacy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHM_ROSTER_MAX 32
#define PHM_SPOOL_MAX  16

typedef enum {
    PhmRoleUnknown,
    PhmRoleTone, /* never carries text at all       */
    PhmRoleCallback, /* numbers only: ring this number  */
    PhmRoleClinical, /* medical vocabulary              */
    PhmRoleDispatch, /* incidents and responses         */
    PhmRoleAdmin, /* text, but nothing urgent        */
    PhmRoleCount,
} PhmRole;

typedef struct {
    uint32_t ric;
    uint16_t pages;
    uint32_t first_ms;
    uint32_t last_ms;
    uint16_t flags; /* union of every leak seen on this capcode */
    uint8_t worst_score;
    uint8_t worst_grade;
    uint8_t kinds; /* bitmask of 1 << PhmPageKind              */
    uint8_t role;
    uint8_t clinical;
    uint8_t incident;
    uint8_t named; /* pages that named a person                */
    uint8_t located; /* pages that gave a location               */
} PhmPager;

typedef struct {
    PhmPager item[PHM_ROSTER_MAX];
    uint8_t count;
    uint16_t overflow; /* capcodes seen after the table filled up  */
} PhmRoster;

typedef struct {
    PhmPage page;
    PhmExposure exposure;
    uint32_t tick_ms;
    uint32_t frequency;
    uint8_t baud_idx;
    int8_t rssi;
} PhmRecord;

typedef struct {
    PhmRecord item[PHM_SPOOL_MAX];
    uint8_t head; /* next slot to write                       */
    uint8_t count;
    uint16_t total; /* pages heard this session, ring or not    */

    /* The worst page of the session is kept whatever happens, so a report can
     * still cite it after it has scrolled out of the ring. */
    PhmRecord worst;
    bool have_worst;
} PhmSpool;

typedef struct {
    uint16_t pages;
    uint16_t capcodes;
    uint16_t named_pages;
    uint16_t located_pages;
    uint16_t flags;
    uint8_t worst_score;
    uint8_t worst_grade;
    uint32_t first_ms;
    uint32_t last_ms;
} PhmTally;

void phm_spool_reset(PhmSpool* spool);

/** Store a page. Overwrites the oldest once the ring is full. */
void phm_spool_add(PhmSpool* spool, const PhmRecord* record);

/** Newest first. NULL past the end. */
const PhmRecord* phm_spool_at(const PhmSpool* spool, uint8_t index);

void phm_roster_reset(PhmRoster* roster);

/** Fold a page into its capcode's entry, creating one if there is room. */
void phm_roster_add(PhmRoster* roster, const PhmRecord* record);

/** Sort busiest first, then by worst grade. Stable enough for a list view. */
void phm_roster_sort(PhmRoster* roster);

const PhmPager* phm_roster_at(const PhmRoster* roster, uint8_t index);

/** The capcode heard most often, or NULL if nothing has been heard. */
const PhmPager* phm_roster_busiest(const PhmRoster* roster);

void phm_tally(const PhmRoster* roster, const PhmSpool* spool, PhmTally* out);

const char* phm_role_name(uint8_t role);
const char* phm_role_hint(uint8_t role);

#ifdef __cplusplus
}
#endif
