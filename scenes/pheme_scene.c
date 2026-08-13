#include "pheme_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const pheme_scene_on_enter_handlers[])(void*) = {
#include "pheme_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const pheme_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "pheme_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const pheme_scene_on_exit_handlers[])(void*) = {
#include "pheme_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers pheme_scene_handlers = {
    .on_enter_handlers = pheme_scene_on_enter_handlers,
    .on_event_handlers = pheme_scene_on_event_handlers,
    .on_exit_handlers = pheme_scene_on_exit_handlers,
    .scene_num = PhemeSceneNum,
};
