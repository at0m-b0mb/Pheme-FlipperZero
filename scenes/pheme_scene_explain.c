#include "../pheme_i.h"

void pheme_scene_explain_on_enter(void* context) {
    PhemeApp* app = context;
    explain_view_set_lesson(
        app->explain_view, scene_manager_get_scene_state(app->scene_manager, PhemeSceneExplain));
    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewExplain);
}

bool pheme_scene_explain_on_event(void* context, SceneManagerEvent event) {
    PhemeApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        explain_view_tick(app->explain_view);
        return true;
    }

    return false;
}

void pheme_scene_explain_on_exit(void* context) {
    UNUSED(context);
}
