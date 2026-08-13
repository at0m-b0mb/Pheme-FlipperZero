#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "pheme_icons.h" // generated from icons/ by fbt

#include "helpers/phm_pocsag.h"
#include "helpers/phm_privacy.h"
#include "helpers/phm_radio.h"
#include "helpers/phm_report.h"
#include "helpers/phm_roster.h"
#include "helpers/phm_settings.h"
#include "scenes/pheme_scene.h"
#include "views/explain_view.h"
#include "views/listen_view.h"
#include "views/message_view.h"
#include "views/roster_view.h"
#include "views/scan_view.h"

#define PHEME_VERSION "1.0"

typedef enum {
    PhemeViewSubmenu,
    PhemeViewListen,
    PhemeViewMessage,
    PhemeViewRoster,
    PhemeViewScan,
    PhemeViewExplain,
    PhemeViewSettings,
    PhemeViewWidget,
    PhemeViewPopup,
} PhemeViewId;

typedef enum {
    PhemeCustomEventPage = 100, /* the decoder produced a page  */
    PhemeCustomEventOpenPages, /* OK on the listen screen      */
    PhemeCustomEventTune, /* scan picked a channel        */
} PhemeCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;
    Popup* popup;

    ListenView* listen_view;
    MessageView* message_view;
    RosterView* roster_view;
    ScanView* scan_view;
    ExplainView* explain_view;

    PhmRadio* radio;
    PhmSettings settings;

    /* True when the listen scene should run the software channel instead of
     * powering the radio. */
    bool demo_mode;

    /* Which page the message scene is showing, newest is zero. */
    uint8_t message_index;

    /* Where the last report landed, for the confirmation screen. */
    char report_path[96];
} PhemeApp;

/** Push the current settings into the radio. */
void pheme_apply_settings(PhemeApp* app);

/** Feedback on a new page, all gated by settings. */
void pheme_notify_page(PhemeApp* app, uint8_t grade);
