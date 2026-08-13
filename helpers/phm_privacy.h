/*
 * phm_privacy - what a page gives away, and how badly.
 *
 * The decoder answers "what was sent". This answers the question the app exists
 * to ask: what did a stranger standing in the car park just learn about someone
 * who never agreed to tell them?
 *
 * Two things come out of a classification:
 *
 *   - Spans. Every stretch of the message that is somebody's name, location,
 *     number or circumstance is marked. The UI uses them to *redact* the
 *     message by default, so the shape of a leak can be shown without the
 *     contents being splashed across a screen in a public place.
 *
 *   - A grade, A+ to F. Scored from what was found, then pushed up against a
 *     set of floors that no page can duck under.
 *
 * The floors are the honest part, and they are why an A+ is unreachable:
 *
 *   Every page      -> at best B. It went out in clear, unauthenticated, to
 *                      every receiver in range, and its capcode is a permanent
 *                      identifier for one pager. Even a page that says nothing
 *                      at all tells you that a particular pager was called, and
 *                      when. There is no such thing as a private POCSAG page.
 *   Any operational
 *   detail          -> at best C.
 *   Any personal
 *   identifier      -> at best D.
 *   Identity and
 *   location, or a
 *   door code       -> F.
 *
 * Floors are applied by compressing the score into the range above them, never
 * by clipping it. Clipping would make every page in a category score exactly
 * the same, and the ordering between them would come from nothing but the order
 * the detectors happened to run in.
 *
 * No furi_ headers: the whole engine is host-testable, and the mockups in the
 * README are rendered from its real output.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PhmLeakName,
    PhmLeakDob,
    PhmLeakPatientId,
    PhmLeakEmail,
    PhmLeakPhone,
    PhmLeakExtension,
    PhmLeakAddress,
    PhmLeakPostcode,
    PhmLeakRoom,
    PhmLeakClinical,
    PhmLeakIncident,
    PhmLeakCredential,
    PhmLeakVehicle,
    PhmLeakLongNumber,
    PhmLeakCount,
} PhmLeak;

typedef enum {
    PhmFamilyIdentity,
    PhmFamilyLocation,
    PhmFamilyContact,
    PhmFamilyContext,
    PhmFamilySecret,
    PhmFamilyCount,
} PhmFamily;

typedef enum {
    PhmGradeAPlus,
    PhmGradeA,
    PhmGradeB,
    PhmGradeC,
    PhmGradeD,
    PhmGradeE,
    PhmGradeF,
    PhmGradeCount,
} PhmGrade;

/* Why a page could not score better than it did. */
typedef enum {
    PhmFloorNone,
    PhmFloorCleartext, /* it is a POCSAG page at all       */
    PhmFloorOperational, /* a real place, callback or event  */
    PhmFloorIdentity, /* a person is identifiable         */
    PhmFloorTracking, /* a person *and* where they are    */
    PhmFloorSecret, /* a code that opens something      */
    PhmFloorCount,
} PhmFloor;

#define PHM_SPAN_MAX 12

typedef struct {
    uint8_t start;
    uint8_t len;
    uint8_t leak; /* PhmLeak */
} PhmSpan;

typedef struct {
    uint16_t flags; /* bitmask of PhmLeak                       */
    uint8_t raw; /* 0..100 before the floors                 */
    uint8_t score; /* 0..100, higher is more exposed           */
    uint8_t grade; /* PhmGrade                                 */
    uint8_t floor; /* PhmFloor that bound it                   */
    uint8_t chars;
    uint8_t n_spans;
    uint8_t redacted_chars; /* characters covered by some span          */
    PhmSpan span[PHM_SPAN_MAX];
} PhmExposure;

/**
 * Classify one page's text. Pass len 0 for a tone-only page: it still gets a
 * grade, because being paged at all is information about you.
 */
void phm_privacy_classify(const char* text, uint8_t len, PhmExposure* out);

/** True when the character at this index falls inside a marked span. */
bool phm_privacy_is_sensitive(const PhmExposure* exposure, uint8_t index);

/** The leak covering this index, or PhmLeakCount if none does. */
uint8_t phm_privacy_leak_at(const PhmExposure* exposure, uint8_t index);

/**
 * Render the message with every span blanked out, for the SD-card report.
 * Sensitive characters become '#', ordinary ones survive, so a report shows the
 * structure of what leaked without carrying the leak itself off the device.
 */
uint8_t phm_privacy_redact(
    const char* text,
    uint8_t len,
    const PhmExposure* exposure,
    char* out,
    uint8_t out_max);

uint8_t phm_grade_of_score(uint8_t score);
const char* phm_grade_name(uint8_t grade);

const char* phm_leak_short(uint8_t leak); /* "name", "ward", "phone"        */
const char* phm_leak_long(uint8_t leak); /* "a person's name"              */
uint8_t phm_leak_family(uint8_t leak);
const char* phm_family_name(uint8_t family);

/** One line explaining what stopped this page scoring better. */
const char* phm_floor_reason(uint8_t floor);

#ifdef __cplusplus
}
#endif
