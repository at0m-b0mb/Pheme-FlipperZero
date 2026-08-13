#include "../pheme_i.h"

static void pheme_message_show(PhemeApp* app) {
    uint8_t total = phm_radio_page_count(app->radio);
    if(total == 0) {
        message_view_set_record(app->message_view, NULL, 0, 0);
        return;
    }

    if(app->message_index >= total) app->message_index = (uint8_t)(total - 1u);

    PhmRecord record;
    if(phm_radio_page_at(app->radio, app->message_index, &record)) {
        message_view_set_record(app->message_view, &record, app->message_index, total);
    }
}

static void pheme_message_step(void* context, int8_t delta) {
    PhemeApp* app = context;
    uint8_t total = phm_radio_page_count(app->radio);
    if(total == 0) return;

    int16_t next = (int16_t)app->message_index + delta;
    if(next < 0) next = 0;
    if(next >= (int16_t)total) next = (int16_t)(total - 1);

    app->message_index = (uint8_t)next;
    pheme_message_show(app);
}

void pheme_scene_message_on_enter(void* context) {
    PhemeApp* app = context;

    message_view_set_reveal_allowed(app->message_view, app->settings.reveal_allowed);
    message_view_set_step_callback(app->message_view, pheme_message_step, app);
    pheme_message_show(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewMessage);
}

bool pheme_scene_message_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void pheme_scene_message_on_exit(void* context) {
    PhemeApp* app = context;
    message_view_set_step_callback(app->message_view, NULL, NULL);
    /* Leaving the reader re-hides whatever was revealed. */
    message_view_set_record(app->message_view, NULL, 0, 0);
}
