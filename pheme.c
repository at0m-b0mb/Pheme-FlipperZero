/*
 * Pheme - a POCSAG pager privacy educator for the Flipper Zero.
 *
 * Hospitals, fire brigades, plant rooms and restaurant coaster pagers still run
 * on POCSAG, a protocol standardised in 1982 that has no encryption and no
 * authentication of any kind. Pheme listens to a paging channel, decodes what
 * is on it, and grades how much of somebody's life just went out over the air
 * in the clear - showing the shape of the leak by default and the words
 * themselves only if you go out of your way to ask.
 *
 * Receive only. There is no transmit path anywhere in this application.
 */
#include "pheme_i.h"

#define TAG "Pheme"

void pheme_apply_settings(PhemeApp* app) {
    furi_assert(app);
    phm_radio_configure(
        app->radio, app->settings.channel_idx, app->settings.baud_lock,
        app->settings.narrow_filter);
    message_view_set_reveal_allowed(app->message_view, app->settings.reveal_allowed);
}

void pheme_notify_page(PhemeApp* app, uint8_t grade) {
    furi_assert(app);

    if(app->settings.led) {
        notification_message(app->notifications, &sequence_blink_blue_10);
    }

    /* A worse page gets a heavier alert. Sequences of different lengths cannot
     * share a ternary, so this is spelled out. */
    if(app->settings.sound || app->settings.vibro) {
        if(grade >= PhmGradeE) {
            notification_message(app->notifications, &sequence_double_vibro);
        } else {
            notification_message(app->notifications, &sequence_semi_success);
        }
    }
}

static bool pheme_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    PhemeApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool pheme_back_event_callback(void* context) {
    furi_assert(context);
    PhemeApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void pheme_tick_event_callback(void* context) {
    furi_assert(context);
    PhemeApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static PhemeApp* pheme_app_alloc(void) {
    PhemeApp* app = malloc(sizeof(PhemeApp));
    memset(app, 0, sizeof(PhemeApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&pheme_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, pheme_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, pheme_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, pheme_tick_event_callback, 100);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, PhemeViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, PhemeViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, PhemeViewPopup, popup_get_view(app->popup));

    app->listen_view = listen_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewListen, listen_view_get_view(app->listen_view));

    app->message_view = message_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewMessage, message_view_get_view(app->message_view));

    app->roster_view = roster_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewRoster, roster_view_get_view(app->roster_view));

    app->scan_view = scan_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewScan, scan_view_get_view(app->scan_view));

    app->explain_view = explain_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, PhemeViewExplain, explain_view_get_view(app->explain_view));

    app->radio = phm_radio_alloc(app->view_dispatcher, PhemeCustomEventPage);

    phm_settings_load(&app->settings);
    pheme_apply_settings(app);

    return app;
}

static void pheme_app_free(PhemeApp* app) {
    furi_assert(app);

    /* The radio owns two threads and the CC1101; nothing else may be torn down
     * until both have stopped. */
    phm_radio_stop_all(app->radio);
    phm_radio_free(app->radio);

    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewListen);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewMessage);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewRoster);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, PhemeViewExplain);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    popup_free(app->popup);
    listen_view_free(app->listen_view);
    message_view_free(app->message_view);
    roster_view_free(app->roster_view);
    scan_view_free(app->scan_view);
    explain_view_free(app->explain_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t pheme_app(void* p) {
    UNUSED(p);

    PhemeApp* app = pheme_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, PhemeSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    pheme_app_free(app);
    return 0;
}
