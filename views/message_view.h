/*
 * One page, read three ways.
 *
 * The default panel does not show the message. It shows its shape - a block per
 * character, tall where the classifier found somebody's data - along with what
 * kinds of data those were. That is enough to make the point in a corridor or a
 * lecture theatre without putting a stranger's name on a screen, and it is why
 * the plain text sits behind a deliberate long press and a setting that starts
 * turned off.
 *
 * Up and Down move between the redaction, the reasoning behind the grade, and
 * the signal detail. Left and Right move between pages.
 */
#pragma once

#include "../helpers/phm_roster.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MessageView MessageView;

typedef void (*MessageStepCallback)(void* context, int8_t delta);

MessageView* message_view_alloc(void);
void message_view_free(MessageView* view);
View* message_view_get_view(MessageView* view);

void message_view_set_record(
    MessageView* view,
    const PhmRecord* record,
    uint8_t index,
    uint8_t total);

/** Without this, a long press on OK does nothing but say why. */
void message_view_set_reveal_allowed(MessageView* view, bool allowed);

void message_view_set_step_callback(MessageView* view, MessageStepCallback callback, void* context);

#ifdef __cplusplus
}
#endif
