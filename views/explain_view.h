/*
 * Five animated lessons about why paging is the way it is.
 *
 * Not a manual for the app - an explanation of the protocol, drawn rather than
 * described, because the shape of a POCSAG batch is the argument. Once you have
 * watched the sync word arrive and the eight frames go past, "your pager only
 * listens to one of them" stops being a fact about power saving and starts
 * being a fact about who else is listening to the other seven.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHM_LESSON_COUNT 5

typedef struct ExplainView ExplainView;

ExplainView* explain_view_alloc(void);
void explain_view_free(ExplainView* view);
View* explain_view_get_view(ExplainView* view);

void explain_view_set_lesson(ExplainView* view, uint8_t lesson);
void explain_view_tick(ExplainView* view);

#ifdef __cplusplus
}
#endif
