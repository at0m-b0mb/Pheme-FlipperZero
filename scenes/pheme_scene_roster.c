#include "../pheme_i.h"

static void pheme_roster_refresh(PhemeApp* app) {
    PhmPager pagers[PHM_ROSTER_MAX];
    uint8_t count = phm_radio_roster_snapshot(app->radio, pagers, PHM_ROSTER_MAX);

    roster_view_set_data(
        app->roster_view, pagers, count, phm_radio_roster_overflow(app->radio));
}

void pheme_scene_roster_on_enter(void* context) {
    PhemeApp* app = context;
    pheme_roster_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewRoster);
}

bool pheme_scene_roster_on_event(void* context, SceneManagerEvent event) {
    PhemeApp* app = context;

    /* The log is live: if the radio is still running behind this screen, the
     * counts keep climbing while you read them. */
    if(event.type == SceneManagerEventTypeTick) {
        pheme_roster_refresh(app);
        return true;
    }

    return false;
}

void pheme_scene_roster_on_exit(void* context) {
    UNUSED(context);
}
