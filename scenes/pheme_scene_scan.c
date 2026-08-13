#include "../pheme_i.h"

static void pheme_scan_pick_callback(void* context, uint8_t channel_idx) {
    PhemeApp* app = context;
    app->settings.channel_idx = channel_idx;
    view_dispatcher_send_custom_event(app->view_dispatcher, PhemeCustomEventTune);
}

static void pheme_scan_refresh(PhemeApp* app) {
    PhmScanBand bands[PHM_CHANNEL_COUNT];
    uint8_t count = phm_radio_scan_snapshot(app->radio, bands, PHM_CHANNEL_COUNT);

    /* Which channel the sweep is dwelling on, so the list visibly moves. */
    uint32_t frequency = phm_radio_frequency(app->radio);
    uint8_t current = 0;
    for(uint8_t i = 0; i < PHM_CHANNEL_COUNT; i++) {
        if(phm_channels[i].frequency == frequency) {
            current = i;
            break;
        }
    }

    scan_view_set_data(
        app->scan_view, bands, count, phm_radio_scan_sweeps(app->radio), current);
}

void pheme_scene_scan_on_enter(void* context) {
    PhemeApp* app = context;

    scan_view_set_pick_callback(app->scan_view, pheme_scan_pick_callback, app);
    phm_radio_scan_start(app->radio);
    pheme_scan_refresh(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewScan);
}

bool pheme_scene_scan_on_event(void* context, SceneManagerEvent event) {
    PhemeApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        pheme_scan_refresh(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == PhemeCustomEventTune) {
        phm_settings_save(&app->settings);
        app->demo_mode = false;
        phm_radio_reset_session(app->radio);
        scene_manager_next_scene(app->scene_manager, PhemeSceneListen);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == PhemeCustomEventPage) {
        /* A page found mid-sweep is worth keeping, but the scan screen has
         * nothing to say about it beyond the count it already shows. */
        return true;
    }

    return false;
}

void pheme_scene_scan_on_exit(void* context) {
    PhemeApp* app = context;
    phm_radio_scan_stop(app->radio);
    scan_view_set_pick_callback(app->scan_view, NULL, NULL);
}
