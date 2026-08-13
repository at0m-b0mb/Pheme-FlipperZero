#include "phm_privacy.h"

#include <string.h>

#define PHM_MAX_TOKENS 40

/* ------------------------------------------------------------- vocabulary -- */

/*
 * Weights. Not tuned to hit a target grade - chosen by asking, for each kind of
 * leak, how much harm it does on its own to the person it is about. A door code
 * outscores a name because a name identifies someone while a code lets you in.
 */
static const uint8_t phm_weight[PhmLeakCount] = {
    [PhmLeakName] = 26,      [PhmLeakDob] = 20,      [PhmLeakPatientId] = 24,
    [PhmLeakEmail] = 18,     [PhmLeakPhone] = 14,    [PhmLeakExtension] = 8,
    [PhmLeakAddress] = 22,   [PhmLeakPostcode] = 16, [PhmLeakRoom] = 12,
    [PhmLeakClinical] = 12,  [PhmLeakIncident] = 14, [PhmLeakCredential] = 34,
    [PhmLeakVehicle] = 12,   [PhmLeakLongNumber] = 8,
};

static const uint8_t phm_family_of[PhmLeakCount] = {
    [PhmLeakName] = PhmFamilyIdentity,     [PhmLeakDob] = PhmFamilyIdentity,
    [PhmLeakPatientId] = PhmFamilyIdentity, [PhmLeakEmail] = PhmFamilyIdentity,
    [PhmLeakPhone] = PhmFamilyContact,     [PhmLeakExtension] = PhmFamilyContact,
    [PhmLeakAddress] = PhmFamilyLocation,  [PhmLeakPostcode] = PhmFamilyLocation,
    [PhmLeakRoom] = PhmFamilyLocation,     [PhmLeakClinical] = PhmFamilyContext,
    [PhmLeakIncident] = PhmFamilyContext,  [PhmLeakCredential] = PhmFamilySecret,
    [PhmLeakVehicle] = PhmFamilyContext,   [PhmLeakLongNumber] = PhmFamilyContext,
};

static const char* const phm_leak_short_name[PhmLeakCount] = {
    [PhmLeakName] = "name",       [PhmLeakDob] = "date of birth",
    [PhmLeakPatientId] = "patient no", [PhmLeakEmail] = "email",
    [PhmLeakPhone] = "phone",     [PhmLeakExtension] = "callback",
    [PhmLeakAddress] = "address", [PhmLeakPostcode] = "postcode",
    [PhmLeakRoom] = "ward/room",  [PhmLeakClinical] = "clinical",
    [PhmLeakIncident] = "incident", [PhmLeakCredential] = "door code",
    [PhmLeakVehicle] = "vehicle", [PhmLeakLongNumber] = "ref number",
};

static const char* const phm_leak_long_name[PhmLeakCount] = {
    [PhmLeakName] = "a person, by name",
    [PhmLeakDob] = "a date of birth",
    [PhmLeakPatientId] = "a patient or record number",
    [PhmLeakEmail] = "an email address",
    [PhmLeakPhone] = "a telephone number",
    [PhmLeakExtension] = "an internal extension or bleep",
    [PhmLeakAddress] = "a street address",
    [PhmLeakPostcode] = "a postcode",
    [PhmLeakRoom] = "a ward, bay, room or zone",
    [PhmLeakClinical] = "somebody's medical circumstances",
    [PhmLeakIncident] = "an incident in progress",
    [PhmLeakCredential] = "a code that opens something",
    [PhmLeakVehicle] = "a vehicle",
    [PhmLeakLongNumber] = "a reference number",
};

static const char* const phm_family_label[PhmFamilyCount] = {
    [PhmFamilyIdentity] = "Identity", [PhmFamilyLocation] = "Location",
    [PhmFamilyContact] = "Contact",   [PhmFamilyContext] = "Circumstance",
    [PhmFamilySecret] = "Secret",
};

static const char* const phm_grade_label[PhmGradeCount] = {
    [PhmGradeAPlus] = "A+", [PhmGradeA] = "A", [PhmGradeB] = "B", [PhmGradeC] = "C",
    [PhmGradeD] = "D",      [PhmGradeE] = "E", [PhmGradeF] = "F",
};

static const char* const phm_floor_text[PhmFloorCount] = {
    [PhmFloorNone] = "nothing",
    [PhmFloorCleartext] = "sent in clear to every pager in range",
    [PhmFloorOperational] = "names a real place, callback or event",
    [PhmFloorIdentity] = "a person can be identified from this",
    [PhmFloorTracking] = "a named person, and where they are",
    [PhmFloorSecret] = "a code that opens something",
};

/* The floor each condition imposes on the score. B is the best a cleartext page
 * can reach; F is what a page earns when it locates a named person. */
static const uint8_t phm_floor_score[PhmFloorCount] = {
    [PhmFloorNone] = 0,       [PhmFloorCleartext] = 16, [PhmFloorOperational] = 31,
    [PhmFloorIdentity] = 51,  [PhmFloorTracking] = 86,  [PhmFloorSecret] = 86,
};

static const char* const kw_title[] = {
    "MR", "MRS", "MS", "MISS", "DR", "PROF", "SR", "SISTER", "NURSE",
    "PT", "PATIENT", "CONSULTANT", "REV", "SGT", "PC", "CAPT", "NAME", "CALLER",
};

static const char* const kw_patient[] = {
    "NHS", "MRN", "CHI", "URN", "UR", "HOSP", "HOSPNO", "PATID", "RECORD",
};

static const char* const kw_reference[] = {
    "JOB", "REF", "CASE", "TICKET", "INCIDENT", "CAD", "LOG", "ORDER",
};

static const char* const kw_room[] = {
    "WARD",  "BED",   "BAY",  "ROOM", "RM",    "UNIT",  "ZONE",  "THEATRE",
    "SUITE", "LEVEL", "FLOOR", "BLOCK", "WING", "CUBICLE", "LIFT", "GATE",
    "DOCK",  "LAB",   "CLINIC",
};

static const char* const kw_ext[] = {
    "EXT", "EXTN", "BLEEP", "PAGER", "DECT", "BEEP", "CALLBACK",
};

static const char* const kw_secret[] = {
    "CODE", "PIN", "PASSWORD", "PASSCODE", "COMBINATION", "KEYCODE", "ACCESS", "ENTRY",
};

static const char* const kw_street[] = {
    "ROAD",  "RD",     "STREET",  "ST",     "AVENUE",  "AVE",   "LANE",   "LN",
    "CLOSE", "DRIVE",  "WAY",     "COURT",  "PLACE",   "PL",    "TERRACE", "CRESCENT",
    "CRES",  "GROVE",  "GARDENS", "GDNS",   "PARK",    "HILL",  "BLVD",   "SQUARE",
    "SQ",    "MEWS",   "WALK",    "ROW",    "PARADE",  "RISE",
};

static const char* const kw_clinical[] = {
    "CRASH",  "ARREST",  "RESUS",   "CARDIAC",  "TRAUMA",  "PAEDS",  "PAEDIATRIC",
    "NEONATAL", "OBSTETRIC", "PSYCH", "OVERDOSE", "DNR",   "STROKE",  "SEPSIS",
    "ICU",    "ITU",     "HDU",     "NICU",     "TRIAGE",  "ONCALL", "SURGERY",
    "BLOODS", "SCAN",    "MRI",     "XRAY",     "TRANSFER", "ADMIT", "DISCHARGE",
    "CATHLAB", "STEMI",  "SEIZURE", "COLLAPSE", "FALLS",   "DECEASED",
};

static const char* const kw_incident[] = {
    "FIRE",   "ALARM",  "SMOKE",  "EVAC",    "EVACUATE", "INTRUDER", "PANIC",
    "ASSAULT", "RTC",   "RTA",    "COLLISION", "POLICE", "SECURITY", "BREACH",
    "ENTRAPMENT", "FLOOD", "SPILL", "GAS",    "INVESTIGATE", "RESPOND", "MUSTER",
    "LOCKDOWN", "AGGRESSION",
};

static const char* const kw_vehicle[] = {
    "VEH", "REG", "PLATE", "VRM", "AMBULANCE", "APPLIANCE", "TRUCK", "VAN",
};

/*
 * Words that end a name. Without them the name detector runs on past the person
 * and swallows whatever follows: "DR PATEL URGENTLY" reads as a three-word name,
 * and worse, "MR A SAMPLE WARD 4B" eats the ward - which loses the location, and
 * with it the very floor that should have made the page an F. A name is a title
 * and at most two words, and it stops at anything that is plainly not a name.
 */
static const char* const kw_stop[] = {
    "TO",      "FOR",    "AT",       "ON",      "IN",     "OF",     "THE",   "AND",
    "OR",      "VIA",    "RE",       "IS",      "HAS",    "WILL",   "NEEDS", "PLEASE",
    "CALL",    "CALLING", "CALLBACK", "ASAP",   "NOW",    "URGENT", "URGENTLY",
    "ATTEND",  "ATTENDING", "REQUIRED", "BACK",  "ETA",    "WAITING", "ARRIVED",
    "HERE",    "READY",  "CONFIRM",  "CONFIRMED", "CONTACT", "REPORT", "SEE",
    "TAKE",    "MOVE",   "WITH",     "FROM",    "NOT",    "NO",     "YES",   "OK",
    "URGENTLY", "IMMEDIATELY", "REQUEST", "REQUESTED", "PENDING", "DUE",
};

#define COUNT_OF(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* ------------------------------------------------------------- tokenizer -- */

typedef struct {
    uint8_t start;
    uint8_t len;
    uint8_t digits;
    uint8_t alphas;
    bool has_at;
    bool has_slash;
    bool claimed;
} Tok;

static bool is_alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* Token characters include the punctuation that lives *inside* a value - the
 * slashes of a date, the at-sign of an address - so those survive as one token
 * and can be recognised for what they are. */
static bool is_token_char(char c) {
    return is_alnum(c) || c == '@' || c == '.' || c == '/' || c == '\'' || c == '-' || c == '+';
}

static uint8_t tokenize(const char* text, uint8_t len, Tok* tokens, uint8_t max) {
    uint8_t n = 0;
    uint8_t i = 0;

    while(i < len && n < max) {
        while(i < len && !is_token_char(text[i])) i++;
        if(i >= len) break;

        uint8_t start = i;
        while(i < len && is_token_char(text[i])) i++;
        uint8_t end = i;

        /* Trim punctuation that turned out to be punctuation after all. */
        while(end > start && !is_alnum(text[end - 1])) end--;
        while(start < end && !is_alnum(text[start]) && text[start] != '+') start++;
        if(end <= start) continue;

        Tok* t = &tokens[n];
        memset(t, 0, sizeof(*t));
        t->start = start;
        t->len = (uint8_t)(end - start);
        for(uint8_t k = start; k < end; k++) {
            char c = text[k];
            if(is_digit(c))
                t->digits++;
            else if(is_alnum(c))
                t->alphas++;
            else if(c == '@')
                t->has_at = true;
            else if(c == '/' || c == '-' || c == '.')
                t->has_slash = true;
        }
        n++;
    }

    return n;
}

static bool tok_eq(const char* text, const Tok* t, const char* word) {
    uint8_t i = 0;
    for(; i < t->len; i++) {
        if(word[i] == '\0') return false;
        if(upper(text[t->start + i]) != word[i]) return false;
    }
    return word[i] == '\0';
}

static bool tok_in(const char* text, const Tok* t, const char* const* list, uint8_t n) {
    for(uint8_t i = 0; i < n; i++) {
        if(tok_eq(text, t, list[i])) return true;
    }
    return false;
}

/* Any word this file already knows the meaning of. Whatever it is, it is not
 * somebody's surname. */
static bool tok_is_vocabulary(const char* text, const Tok* t) {
    return tok_in(text, t, kw_stop, COUNT_OF(kw_stop)) ||
           tok_in(text, t, kw_title, COUNT_OF(kw_title)) ||
           tok_in(text, t, kw_room, COUNT_OF(kw_room)) ||
           tok_in(text, t, kw_ext, COUNT_OF(kw_ext)) ||
           tok_in(text, t, kw_secret, COUNT_OF(kw_secret)) ||
           tok_in(text, t, kw_street, COUNT_OF(kw_street)) ||
           tok_in(text, t, kw_patient, COUNT_OF(kw_patient)) ||
           tok_in(text, t, kw_reference, COUNT_OF(kw_reference)) ||
           tok_in(text, t, kw_clinical, COUNT_OF(kw_clinical)) ||
           tok_in(text, t, kw_incident, COUNT_OF(kw_incident)) ||
           tok_in(text, t, kw_vehicle, COUNT_OF(kw_vehicle));
}

/* ---------------------------------------------------------------- result -- */

typedef struct {
    PhmExposure* out;
    const char* text;
    Tok* tok;
    uint8_t n_tok;
} Ctx;

static void mark(Ctx* ctx, uint8_t first_tok, uint8_t last_tok, uint8_t leak) {
    if(first_tok > last_tok || last_tok >= ctx->n_tok) return;

    for(uint8_t i = first_tok; i <= last_tok; i++) ctx->tok[i].claimed = true;

    ctx->out->flags |= (uint16_t)(1u << leak);

    if(ctx->out->n_spans >= PHM_SPAN_MAX) return;
    PhmSpan* span = &ctx->out->span[ctx->out->n_spans++];
    span->start = ctx->tok[first_tok].start;
    uint16_t end = (uint16_t)ctx->tok[last_tok].start + ctx->tok[last_tok].len;
    span->len = (uint8_t)(end - span->start);
    span->leak = leak;
}

/* Flag a leak that has no meaningful span - a single vocabulary word tells you
 * what kind of message this is without any one stretch of it being the secret. */
static void flag(Ctx* ctx, uint8_t leak) {
    ctx->out->flags |= (uint16_t)(1u << leak);
}

/* ------------------------------------------------------------- detectors -- */

static bool tok_is_date(const char* text, const Tok* t) {
    /* 01/01/60, 1-1-1960, 01.01.60 - two separators and nothing but digits. */
    if(t->digits < 4 || t->alphas > 0 || !t->has_slash) return false;
    uint8_t seps = 0;
    for(uint8_t i = 0; i < t->len; i++) {
        char c = text[t->start + i];
        if(c == '/' || c == '-' || c == '.') seps++;
    }
    return seps == 2;
}

static bool tok_is_postcode_head(const char* text, const Tok* t) {
    /* One or two letters, then one or two digits, optionally one more letter. */
    if(t->len < 2 || t->len > 4 || t->digits == 0 || t->alphas == 0) return false;
    uint8_t i = 0;
    uint8_t letters = 0;
    while(i < t->len && !is_digit(text[t->start + i])) {
        letters++;
        i++;
    }
    if(letters < 1 || letters > 2) return false;
    uint8_t digits = 0;
    while(i < t->len && is_digit(text[t->start + i])) {
        digits++;
        i++;
    }
    if(digits < 1 || digits > 2) return false;
    return (t->len - i) <= 1;
}

static bool tok_is_postcode_tail(const char* text, const Tok* t) {
    if(t->len != 3) return false;
    return is_digit(text[t->start]) && !is_digit(text[t->start + 1]) &&
           !is_digit(text[t->start + 2]);
}

static bool tok_is_plate(const char* text, const Tok* t) {
    /* The current UK format: two letters, two digits, three letters. */
    if(t->len != 7) return false;
    for(uint8_t i = 0; i < 7; i++) {
        bool want_digit = (i == 2 || i == 3);
        if(is_digit(text[t->start + i]) != want_digit) return false;
    }
    return true;
}

static void detect(Ctx* ctx) {
    const char* text = ctx->text;
    Tok* tok = ctx->tok;
    uint8_t n = ctx->n_tok;

    /*
     * Labelled things go first and claim their tokens, so that the unlabelled
     * sweeps at the end cannot re-report a hospital number as a phone number.
     */

    /* Email. */
    for(uint8_t i = 0; i < n; i++) {
        if(tok[i].has_at && tok[i].alphas >= 3) mark(ctx, i, i, PhmLeakEmail);
    }

    /* A title followed by the person it belongs to. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed || !tok_in(text, &tok[i], kw_title, COUNT_OF(kw_title))) continue;

        /* "PT 4471" is a record, not a person; a name has letters after it. */
        uint8_t last = i;
        uint8_t words = 0;
        uint8_t full = 0;
        for(uint8_t j = (uint8_t)(i + 1); j < n && words < 2; j++) {
            if(tok[j].digits > 0 || tok[j].alphas == 0) break;
            if(tok_is_vocabulary(text, &tok[j])) break;
            last = j;
            words++;
            if(tok[j].len >= 2) full++;
        }

        if(words >= 1 && full >= 1) mark(ctx, i, last, PhmLeakName);
    }

    /* A record number, announced as one. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed) continue;
        bool patient = tok_in(text, &tok[i], kw_patient, COUNT_OF(kw_patient));
        bool reference = tok_in(text, &tok[i], kw_reference, COUNT_OF(kw_reference));
        if(!patient && !reference) continue;
        if(tok[i + 1].digits < 4 || tok[i + 1].alphas > 0) continue;
        mark(ctx, i, (uint8_t)(i + 1), patient ? PhmLeakPatientId : PhmLeakLongNumber);
    }

    /* A date of birth, labelled or bare. */
    for(uint8_t i = 0; i < n; i++) {
        if(tok[i].claimed) continue;
        bool labelled = tok_eq(text, &tok[i], "DOB") || tok_eq(text, &tok[i], "BORN") ||
                        tok_eq(text, &tok[i], "D.O.B");
        if(labelled && i + 1 < n && tok[i + 1].digits >= 4) {
            mark(ctx, i, (uint8_t)(i + 1), PhmLeakDob);
        } else if(!labelled && tok_is_date(text, &tok[i])) {
            mark(ctx, i, i, PhmLeakDob);
        }
    }

    /* A code that opens something. The number is what makes it a secret - "CODE
     * BLUE" is a medical emergency, "CODE 4471" is a door. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed) continue;
        if(!tok_in(text, &tok[i], kw_secret, COUNT_OF(kw_secret))) continue;
        if(tok[i + 1].digits < 3 || tok[i + 1].alphas > 0) continue;
        uint8_t first = (i > 0 && !tok[i - 1].claimed && tok[i - 1].alphas > 2) ? (uint8_t)(i - 1) : i;
        mark(ctx, first, (uint8_t)(i + 1), PhmLeakCredential);
    }

    /* An internal callback. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed) continue;
        if(!tok_in(text, &tok[i], kw_ext, COUNT_OF(kw_ext))) continue;
        if(tok[i + 1].digits < 3 || tok[i + 1].alphas > 0) continue;
        mark(ctx, i, (uint8_t)(i + 1), PhmLeakExtension);
    }

    /* Where in the building. The number can be a token or two away: "BED 12",
     * "LEVEL B2", "ZONE 3 PLANT". */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed) continue;
        if(!tok_in(text, &tok[i], kw_room, COUNT_OF(kw_room))) continue;
        if(tok[i + 1].digits == 0) continue;
        mark(ctx, i, (uint8_t)(i + 1), PhmLeakRoom);
    }

    /* A street address: a house number, then a street name, then a suffix. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed || tok[i].digits == 0 || tok[i].alphas > 1) continue;
        if(tok[i].digits > 4) continue;
        for(uint8_t j = (uint8_t)(i + 1); j < n && j <= i + 3; j++) {
            if(tok[j].claimed) break;
            if(tok_in(text, &tok[j], kw_street, COUNT_OF(kw_street))) {
                mark(ctx, i, j, PhmLeakAddress);
                break;
            }
            if(tok[j].digits > 0) break;
        }
    }

    /* A postcode, in two halves. */
    for(uint8_t i = 0; i + 1 < n; i++) {
        if(tok[i].claimed || tok[i + 1].claimed) continue;
        if(tok_is_postcode_head(text, &tok[i]) && tok_is_postcode_tail(text, &tok[i + 1])) {
            mark(ctx, i, (uint8_t)(i + 1), PhmLeakPostcode);
        }
    }

    /* A registration plate, or a labelled vehicle. */
    for(uint8_t i = 0; i < n; i++) {
        if(tok[i].claimed) continue;
        if(tok_is_plate(text, &tok[i])) {
            mark(ctx, i, i, PhmLeakVehicle);
        } else if(tok_in(text, &tok[i], kw_vehicle, COUNT_OF(kw_vehicle))) {
            flag(ctx, PhmLeakVehicle);
        }
    }

    /* Vocabulary. These describe the circumstance rather than name anything, so
     * they set a flag but claim no span - blanking them out would leave a
     * redacted message that says nothing about why it was sent. */
    for(uint8_t i = 0; i < n; i++) {
        if(tok_in(text, &tok[i], kw_clinical, COUNT_OF(kw_clinical))) flag(ctx, PhmLeakClinical);
        if(tok_in(text, &tok[i], kw_incident, COUNT_OF(kw_incident))) flag(ctx, PhmLeakIncident);
    }

    /* Whatever long numbers are left over. A run of seven or more digits that
     * nobody labelled is a telephone number far more often than it is anything
     * else; six is a reference of some kind. */
    for(uint8_t i = 0; i < n; i++) {
        if(tok[i].claimed || tok[i].alphas > 0) continue;

        uint8_t digits = tok[i].digits;
        uint8_t last = i;
        /* "020 7946 0018" is one number written as three tokens. */
        while(last + 1 < n && !tok[last + 1].claimed && tok[last + 1].alphas == 0 &&
              tok[last + 1].digits >= 3 && digits + tok[last + 1].digits <= 15) {
            last++;
            digits = (uint8_t)(digits + tok[last].digits);
        }

        if(digits >= 7) {
            mark(ctx, i, last, PhmLeakPhone);
            i = last;
        } else if(digits >= 6) {
            mark(ctx, i, last, PhmLeakLongNumber);
            i = last;
        }
    }
}

/* ---------------------------------------------------------------- scoring -- */

static uint8_t pick_floor(uint16_t flags) {
    const uint16_t identity = (1u << PhmLeakName) | (1u << PhmLeakDob) |
                              (1u << PhmLeakPatientId) | (1u << PhmLeakEmail);
    const uint16_t location =
        (1u << PhmLeakAddress) | (1u << PhmLeakPostcode) | (1u << PhmLeakRoom);
    const uint16_t operational = location | (1u << PhmLeakPhone) | (1u << PhmLeakExtension) |
                                 (1u << PhmLeakClinical) | (1u << PhmLeakIncident) |
                                 (1u << PhmLeakVehicle) | (1u << PhmLeakLongNumber);

    if(flags & (1u << PhmLeakCredential)) return PhmFloorSecret;
    if((flags & identity) && (flags & location)) return PhmFloorTracking;
    if(flags & identity) return PhmFloorIdentity;
    if(flags & operational) return PhmFloorOperational;
    return PhmFloorCleartext;
}

void phm_privacy_classify(const char* text, uint8_t len, PhmExposure* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    if(!text) len = 0;
    out->chars = len;

    if(len > 0) {
        Tok tokens[PHM_MAX_TOKENS];
        Ctx ctx = {
            .out = out,
            .text = text,
            .tok = tokens,
            .n_tok = tokenize(text, len, tokens, PHM_MAX_TOKENS),
        };
        detect(&ctx);
    }

    uint16_t raw = 0;
    for(uint8_t leak = 0; leak < PhmLeakCount; leak++) {
        if(out->flags & (1u << leak)) raw += phm_weight[leak];
    }

    /* A longer message is a bigger window into somebody's day even when no
     * single detector fires on it. Small, and capped, because length on its own
     * is the weakest evidence there is. */
    raw += (uint16_t)(len / 10u);
    if(raw > 100u) raw = 100u;
    out->raw = (uint8_t)raw;

    out->floor = pick_floor(out->flags);
    uint8_t floor = phm_floor_score[out->floor];

    /*
     * Compressed into the range above the floor, not clipped to it. Clipping
     * would give every page in a category the same score, and two pages that
     * leak very different amounts would then be separated only by whichever
     * detector happened to run first.
     */
    out->score = (uint8_t)(floor + ((100u - floor) * raw) / 100u);
    out->grade = phm_grade_of_score(out->score);

    for(uint8_t i = 0; i < out->n_spans; i++) out->redacted_chars += out->span[i].len;
}

uint8_t phm_grade_of_score(uint8_t score) {
    if(score <= 5) return PhmGradeAPlus;
    if(score <= 15) return PhmGradeA;
    if(score <= 30) return PhmGradeB;
    if(score <= 50) return PhmGradeC;
    if(score <= 70) return PhmGradeD;
    if(score <= 85) return PhmGradeE;
    return PhmGradeF;
}

/* ------------------------------------------------------------------ spans -- */

bool phm_privacy_is_sensitive(const PhmExposure* exposure, uint8_t index) {
    return phm_privacy_leak_at(exposure, index) != PhmLeakCount;
}

uint8_t phm_privacy_leak_at(const PhmExposure* exposure, uint8_t index) {
    if(!exposure) return PhmLeakCount;
    for(uint8_t i = 0; i < exposure->n_spans; i++) {
        const PhmSpan* span = &exposure->span[i];
        if(index >= span->start && index < (uint16_t)span->start + span->len) return span->leak;
    }
    return PhmLeakCount;
}

uint8_t phm_privacy_redact(
    const char* text,
    uint8_t len,
    const PhmExposure* exposure,
    char* out,
    uint8_t out_max) {
    if(!out || out_max == 0) return 0;
    out[0] = '\0';
    if(!text) return 0;

    uint8_t n = 0;
    for(uint8_t i = 0; i < len && n + 1 < out_max; i++) {
        char c = text[i];
        if(phm_privacy_is_sensitive(exposure, i) && c != ' ') c = '#';
        out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ names -- */

const char* phm_grade_name(uint8_t grade) {
    return (grade < PhmGradeCount) ? phm_grade_label[grade] : "?";
}

const char* phm_leak_short(uint8_t leak) {
    return (leak < PhmLeakCount) ? phm_leak_short_name[leak] : "?";
}

const char* phm_leak_long(uint8_t leak) {
    return (leak < PhmLeakCount) ? phm_leak_long_name[leak] : "?";
}

uint8_t phm_leak_family(uint8_t leak) {
    return (leak < PhmLeakCount) ? phm_family_of[leak] : PhmFamilyContext;
}

const char* phm_family_name(uint8_t family) {
    return (family < PhmFamilyCount) ? phm_family_label[family] : "?";
}

const char* phm_floor_reason(uint8_t floor) {
    return (floor < PhmFloorCount) ? phm_floor_text[floor] : "?";
}
