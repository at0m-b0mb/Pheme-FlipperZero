#include "../pheme_i.h"

static const char* const rate_names[] = {"Auto", "512", "1200", "2400"};
static const char* const filter_names[] = {"Standard", "Narrow"};
static const char* const off_on[] = {"Off", "On"};

static void pheme_settings_channel(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.channel_idx = index;
    variable_item_set_current_value_text(item, phm_channels[index].label);
}

static void pheme_settings_rate(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.baud_lock = (index == 0) ? (int8_t)-1 : (int8_t)(index - 1);
    variable_item_set_current_value_text(item, rate_names[index]);
}

static void pheme_settings_filter(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.narrow_filter = (index == 1);
    variable_item_set_current_value_text(item, filter_names[index]);
}

static void pheme_settings_reveal(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.reveal_allowed = (index == 1);
    variable_item_set_current_value_text(item, off_on[index]);
}

static void pheme_settings_sound(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.sound = (index == 1);
    variable_item_set_current_value_text(item, off_on[index]);
}

static void pheme_settings_vibro(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.vibro = (index == 1);
    variable_item_set_current_value_text(item, off_on[index]);
}

static void pheme_settings_led(VariableItem* item) {
    PhemeApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.led = (index == 1);
    variable_item_set_current_value_text(item, off_on[index]);
}

void pheme_scene_settings_on_enter(void* context) {
    PhemeApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(
        list, "Channel", PHM_CHANNEL_COUNT, pheme_settings_channel, app);
    variable_item_set_current_value_index(item, app->settings.channel_idx);
    variable_item_set_current_value_text(item, phm_channels[app->settings.channel_idx].label);

    uint8_t rate_index = (app->settings.baud_lock < 0) ? 0u : (uint8_t)(app->settings.baud_lock + 1);
    item = variable_item_list_add(list, "Rate", COUNT_OF(rate_names), pheme_settings_rate, app);
    variable_item_set_current_value_index(item, rate_index);
    variable_item_set_current_value_text(item, rate_names[rate_index]);

    item = variable_item_list_add(list, "Filter", COUNT_OF(filter_names), pheme_settings_filter, app);
    variable_item_set_current_value_index(item, app->settings.narrow_filter ? 1 : 0);
    variable_item_set_current_value_text(item, filter_names[app->settings.narrow_filter ? 1 : 0]);

    /*
     * Off by default and left at the bottom of no list: this is the switch that
     * turns a privacy demonstration into a device that shows strangers' messages
     * on a screen. The app works completely without it.
     */
    item = variable_item_list_add(list, "Reveal text", COUNT_OF(off_on), pheme_settings_reveal, app);
    variable_item_set_current_value_index(item, app->settings.reveal_allowed ? 1 : 0);
    variable_item_set_current_value_text(item, off_on[app->settings.reveal_allowed ? 1 : 0]);

    item = variable_item_list_add(list, "Sound", COUNT_OF(off_on), pheme_settings_sound, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, off_on[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibro", COUNT_OF(off_on), pheme_settings_vibro, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, off_on[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(list, "LED", COUNT_OF(off_on), pheme_settings_led, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, off_on[app->settings.led ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewSettings);
}

bool pheme_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void pheme_scene_settings_on_exit(void* context) {
    PhemeApp* app = context;
    variable_item_list_reset(app->var_item_list);
    phm_settings_save(&app->settings);
    pheme_apply_settings(app);
}
