#include "phm_settings.h"

#include "phm_pocsag.h"
#include "phm_radio.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define TAG "Pheme"

#define PHM_SETTINGS_PATH    APP_DATA_PATH("pheme.conf")
#define PHM_SETTINGS_MAGIC   0x7E
#define PHM_SETTINGS_VERSION 1

void phm_settings_default(PhmSettings* settings) {
    furi_assert(settings);
    settings->channel_idx = PHM_CHANNEL_DEFAULT;
    settings->baud_lock = -1;
    settings->narrow_filter = false;
    /*
     * Off by default, and deliberately so. Pheme's whole argument is that these
     * messages are about people who never agreed to be read, so the plain text
     * stays behind a switch the user has to reach for on purpose.
     */
    settings->reveal_allowed = false;
    settings->sound = true;
    settings->vibro = true;
    settings->led = true;
    settings->auto_report = false;
}

/* A hand-edited or half-written file must never be able to index off the end of
 * phm_channels or phm_bauds, so every field is re-checked on load. */
static bool phm_settings_valid(const PhmSettings* settings) {
    return settings->channel_idx < PHM_CHANNEL_COUNT && settings->baud_lock >= -1 &&
           settings->baud_lock < (int8_t)PHM_BAUD_COUNT;
}

void phm_settings_load(PhmSettings* settings) {
    furi_assert(settings);
    phm_settings_default(settings);

    PhmSettings loaded;
    if(!saved_struct_load(
           PHM_SETTINGS_PATH,
           &loaded,
           sizeof(PhmSettings),
           PHM_SETTINGS_MAGIC,
           PHM_SETTINGS_VERSION)) {
        FURI_LOG_D(TAG, "no saved settings, using defaults");
        return;
    }

    if(!phm_settings_valid(&loaded)) {
        FURI_LOG_W(TAG, "saved settings out of range, using defaults");
        return;
    }

    *settings = loaded;
}

void phm_settings_save(const PhmSettings* settings) {
    furi_assert(settings);

    /* The app data directory is created lazily; make sure it exists before the
     * first write, otherwise saved_struct_save has nowhere to land. */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);

    if(!saved_struct_save(
           PHM_SETTINGS_PATH,
           settings,
           sizeof(PhmSettings),
           PHM_SETTINGS_MAGIC,
           PHM_SETTINGS_VERSION)) {
        FURI_LOG_W(TAG, "could not save settings");
    }
}
