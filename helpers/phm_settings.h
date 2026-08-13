#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t channel_idx;
    int8_t baud_lock; /* -1 = decode all three rates          */
    bool narrow_filter; /* tighten the CC1101 receive filter    */
    bool reveal_allowed; /* unmasking a message needs this on    */
    bool sound;
    bool vibro;
    bool led;
    bool auto_report; /* write a redacted report on exit      */
} PhmSettings;

void phm_settings_default(PhmSettings* settings);
void phm_settings_load(PhmSettings* settings);
void phm_settings_save(const PhmSettings* settings);

#ifdef __cplusplus
}
#endif
