#include "../pheme_i.h"

static void pheme_report_popup_callback(void* context) {
    PhemeApp* app = context;
    scene_manager_previous_scene(app->scene_manager);
}

void pheme_scene_report_on_enter(void* context) {
    PhemeApp* app = context;

    popup_reset(app->popup);

    bool ok = phm_report_write(app->radio, app->report_path, sizeof(app->report_path));

    if(ok) {
        /* Show the file name rather than the whole path: the directory is
         * always the same and the name is the part worth reading off. */
        const char* name = strrchr(app->report_path, '/');
        name = name ? name + 1 : app->report_path;

        popup_set_header(app->popup, "Report saved", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, name, 64, 26, AlignCenter, AlignTop);
    } else {
        popup_set_header(app->popup, "Nothing to report", 64, 8, AlignCenter, AlignTop);
        popup_set_text(
            app->popup,
            "Listen to a channel first.\nNo pages, no report.",
            64,
            26,
            AlignCenter,
            AlignTop);
    }

    popup_set_timeout(app->popup, 2500);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, pheme_report_popup_callback);
    popup_enable_timeout(app->popup);

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewPopup);
}

bool pheme_scene_report_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void pheme_scene_report_on_exit(void* context) {
    PhemeApp* app = context;
    popup_reset(app->popup);
}
