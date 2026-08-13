#pragma once

#include "../helpers/phm_roster.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RosterView RosterView;

RosterView* roster_view_alloc(void);
void roster_view_free(RosterView* view);
View* roster_view_get_view(RosterView* view);

void roster_view_set_data(RosterView* view, const PhmPager* pagers, uint8_t count, uint16_t overflow);

#ifdef __cplusplus
}
#endif
