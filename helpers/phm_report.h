/*
 * phm_report - a session, written to the SD card with the leak taken out.
 *
 * The report is the one thing Pheme produces that outlives the session, so it
 * is the one place where getting privacy wrong would actually matter: a file
 * full of other people's names sitting on a memory card is exactly the harm the
 * app is complaining about. Every message in it is passed through the redactor
 * first, so what survives is the shape of the leak - which capcode, what kinds
 * of personal data, what grade - and never the data itself.
 */
#pragma once

#include "phm_radio.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write a redacted report of the current session.
 *
 * @param path_out  receives the file path written, for the confirmation screen
 * @return          false if there was nothing to report, or the write failed
 */
bool phm_report_write(const PhmRadio* radio, char* path_out, size_t path_max);

#ifdef __cplusplus
}
#endif
