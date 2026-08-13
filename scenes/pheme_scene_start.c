#include "../pheme_i.h"

typedef enum {
    StartIndexListen,
    StartIndexDemo,
    StartIndexScan,
    StartIndexRoster,
    StartIndexReport,
    StartIndexExplain,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void pheme_scene_start_callback(void* context, uint32_t index) {
    PhemeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void pheme_scene_start_on_enter(void* context) {
    PhemeApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Pheme");

    submenu_add_item(submenu, "Listen", StartIndexListen, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "Demo channel", StartIndexDemo, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "Scan channels", StartIndexScan, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "Pager log", StartIndexRoster, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "Save report", StartIndexReport, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "How paging works", StartIndexExplain, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, pheme_scene_start_callback, app);
    submenu_add_item(submenu, "About", StartIndexAbout, pheme_scene_start_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, PhemeSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewSubmenu);
}

bool pheme_scene_start_on_event(void* context, SceneManagerEvent event) {
    PhemeApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, PhemeSceneStart, event.event);

    switch(event.event) {
    case StartIndexListen:
    case StartIndexDemo:
        /* Choosing to listen is what starts a session, so this is where the
         * spool, the roster and the clock are cleared - not on every re-entry
         * to the listen screen. */
        app->demo_mode = (event.event == StartIndexDemo);
        phm_radio_reset_session(app->radio);
        scene_manager_next_scene(app->scene_manager, PhemeSceneListen);
        return true;

    case StartIndexScan:
        scene_manager_next_scene(app->scene_manager, PhemeSceneScan);
        return true;

    case StartIndexRoster:
        scene_manager_next_scene(app->scene_manager, PhemeSceneRoster);
        return true;

    case StartIndexReport:
        scene_manager_next_scene(app->scene_manager, PhemeSceneReport);
        return true;

    case StartIndexExplain:
        scene_manager_next_scene(app->scene_manager, PhemeSceneExplain);
        return true;

    case StartIndexSettings:
        scene_manager_next_scene(app->scene_manager, PhemeSceneSettings);
        return true;

    case StartIndexAbout:
        scene_manager_next_scene(app->scene_manager, PhemeSceneAbout);
        return true;

    default:
        return false;
    }
}

void pheme_scene_start_on_exit(void* context) {
    PhemeApp* app = context;
    submenu_reset(app->submenu);
}
