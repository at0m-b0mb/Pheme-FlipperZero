#include "../pheme_i.h"

static void pheme_listen_open_callback(void* context) {
    PhemeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PhemeCustomEventOpenPages);
}

/* Gather everything the screen needs in one pass, so the draw callback never
 * reaches into live radio state. */
static void pheme_listen_refresh(PhemeApp* app) {
    PhmPocsagStatus status;
    phm_radio_status(app->radio, &status);

    PhmTally tally;
    phm_radio_tally(app->radio, &tally);

    ListenData data;
    memset(&data, 0, sizeof(data));

    data.channel = phm_channels[app->settings.channel_idx].label;
    data.use = phm_channels[app->settings.channel_idx].use;
    data.demo = app->demo_mode;

    data.lock = status.lock;
    data.baud_idx = status.baud_idx;
    data.batches = status.batches;
    data.bad_words = status.bad_words;
    data.frame_mask = status.frame_mask;
    data.word_idx = status.word_idx;

    data.pages = tally.pages;
    data.capcodes = tally.capcodes;
    data.rssi = (int8_t)phm_radio_rssi(app->radio);
    data.elapsed_s = phm_radio_elapsed_ms(app->radio) / 1000u;

    PhmRecord record;
    if(phm_radio_page_at(app->radio, 0, &record)) {
        data.have_page = true;
        data.ric = record.page.ric;
        data.kind = record.page.kind;
        data.len = record.page.len;
        memcpy(data.text, record.page.text, sizeof(data.text));
        data.exposure = record.exposure;
    }

    listen_view_set_data(app->listen_view, &data);
}

void pheme_scene_listen_on_enter(void* context) {
    PhemeApp* app = context;

    pheme_apply_settings(app);
    listen_view_set_open_callback(app->listen_view, pheme_listen_open_callback, app);

    if(app->demo_mode) {
        phm_radio_demo_start(app->radio);
    } else {
        phm_radio_listen_start(app->radio);
    }

    pheme_listen_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewListen);
}

bool pheme_scene_listen_on_event(void* context, SceneManagerEvent event) {
    PhemeApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        listen_view_tick(app->listen_view);
        pheme_listen_refresh(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PhemeCustomEventPage) {
            PhmRecord record;
            if(phm_radio_page_at(app->radio, 0, &record)) {
                pheme_notify_page(app, record.exposure.grade);
            }
            pheme_listen_refresh(app);
            return true;
        }

        if(event.event == PhemeCustomEventOpenPages) {
            app->message_index = 0;
            scene_manager_next_scene(app->scene_manager, PhemeSceneMessage);
            return true;
        }
    }

    return false;
}

void pheme_scene_listen_on_exit(void* context) {
    PhemeApp* app = context;

    /*
     * The scene manager runs this when a child scene is pushed as well as when
     * the user leaves, so reading a page powers the radio down and coming back
     * powers it up again. That is deliberate: the session survives the detour
     * because the spool, the roster and the clock all live in the radio object
     * and only a fresh start from the menu clears them.
     */
    phm_radio_stop_all(app->radio);
    listen_view_set_open_callback(app->listen_view, NULL, NULL);
}
