#include "phm_report.h"

#include "phm_privacy.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>

#define TAG "Pheme"

#define PHM_REPORT_DIR APP_DATA_PATH("")

static void phm_append(File* file, FuriString* line) {
    storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
    furi_string_reset(line);
}

/* "name, ward/room, clinical" - the leak flags, spelled out. */
static void phm_flag_list(FuriString* out, uint16_t flags) {
    bool first = true;
    for(uint8_t leak = 0; leak < PhmLeakCount; leak++) {
        if(!(flags & (1u << leak))) continue;
        if(!first) furi_string_cat_str(out, ", ");
        furi_string_cat_str(out, phm_leak_short(leak));
        first = false;
    }
    if(first) furi_string_cat_str(out, "nothing identifiable");
}

bool phm_report_write(const PhmRadio* radio, char* path_out, size_t path_max) {
    furi_assert(radio);

    PhmTally tally;
    phm_radio_tally(radio, &tally);
    if(tally.pages == 0) return false;

    DateTime now;
    furi_hal_rtc_get_datetime(&now);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);

    FuriString* path = furi_string_alloc();
    furi_string_printf(
        path,
        APP_DATA_PATH("pheme_%04u%02u%02u_%02u%02u%02u.txt"),
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS);

    if(ok) {
        FuriString* line = furi_string_alloc();
        uint32_t frequency = phm_radio_frequency(radio);
        uint32_t minutes = phm_radio_elapsed_ms(radio) / 60000u;

        furi_string_printf(line, "Pheme - POCSAG exposure report\r\n");
        furi_string_cat_printf(
            line,
            "%04u-%02u-%02u %02u:%02u  %lu.%04lu MHz  %lu min listening\r\n\r\n",
            now.year,
            now.month,
            now.day,
            now.hour,
            now.minute,
            (unsigned long)(frequency / 1000000u),
            (unsigned long)((frequency % 1000000u) / 100u),
            (unsigned long)minutes);
        phm_append(file, line);

        furi_string_printf(
            line,
            "%u pages from %u capcodes\r\nworst grade %s (%u/100)\r\n",
            tally.pages,
            tally.capcodes,
            phm_grade_name(tally.worst_grade),
            tally.worst_score);
        furi_string_cat_printf(
            line,
            "%u pages named a person, %u gave a location\r\n\r\n",
            tally.named_pages,
            tally.located_pages);
        phm_append(file, line);

        /* Capcodes, because a capcode is the part that does not change. */
        furi_string_printf(line, "CAPCODES\r\n");
        phm_append(file, line);

        PhmPager roster[PHM_ROSTER_MAX];
        uint8_t n = phm_radio_roster_snapshot(radio, roster, PHM_ROSTER_MAX);
        for(uint8_t i = 0; i < n; i++) {
            furi_string_printf(
                line,
                "  %-8lu %3u pages  %-9s  worst %-2s  ",
                (unsigned long)roster[i].ric,
                roster[i].pages,
                phm_role_name(roster[i].role),
                phm_grade_name(roster[i].worst_grade));
            phm_flag_list(line, roster[i].flags);
            furi_string_cat_str(line, "\r\n");
            phm_append(file, line);
        }

        furi_string_printf(line, "\r\nPAGES (message text redacted)\r\n");
        phm_append(file, line);

        char redacted[PHM_TEXT_MAX];
        uint8_t pages = phm_radio_page_count(radio);
        for(uint8_t i = 0; i < pages; i++) {
            PhmRecord record;
            if(!phm_radio_page_at(radio, i, &record)) continue;

            phm_privacy_redact(
                record.page.text, record.page.len, &record.exposure, redacted, sizeof(redacted));

            furi_string_printf(
                line,
                "  [%-2s %3u] %-8lu %-7s %4s bps %4d dBm\r\n",
                phm_grade_name(record.exposure.grade),
                record.exposure.score,
                (unsigned long)record.page.ric,
                phm_page_kind_name(record.page.kind),
                phm_bauds[record.baud_idx % PHM_BAUD_COUNT].label,
                record.rssi);
            if(record.page.len) {
                furi_string_cat_printf(line, "           \"%s\"\r\n", redacted);
            }
            furi_string_cat_str(line, "           ");
            phm_flag_list(line, record.exposure.flags);
            furi_string_cat_printf(
                line, "\r\n           capped at %s\r\n", phm_floor_reason(record.exposure.floor));
            phm_append(file, line);
        }

        furi_string_printf(
            line,
            "\r\nPheme transmitted nothing. It cannot: there is no transmit path\r\n"
            "in the app. Every character above that the classifier identified as\r\n"
            "somebody's personal data has been replaced with '#', so this file\r\n"
            "records what leaked without carrying the leak off the device.\r\n"
            "\r\nWhat could not be heard at all: VHF paging below 300 MHz, UK and\r\n"
            "European commercial paging at 466 MHz, and US national paging at\r\n"
            "929-932 MHz are all outside the CC1101's tuning range.\r\n");
        phm_append(file, line);

        furi_string_free(line);
    } else {
        FURI_LOG_W(TAG, "could not open report file");
    }

    storage_file_close(file);
    storage_file_free(file);

    if(ok && path_out && path_max) {
        strncpy(path_out, furi_string_get_cstr(path), path_max - 1);
        path_out[path_max - 1] = '\0';
    }

    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
