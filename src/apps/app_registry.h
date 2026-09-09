#pragma once

#include "app_context.h"

#include <cstddef>
#include <cstdint>

enum class AppId : uint8_t {
    none = 0,
    aichats,
    settings,
    photos,
    music,
    video,
    analyzer,
    gravity,
    squareline,
    clock,
};

struct AppDefinition {
    AppId id;
    const char *name;
    const lv_image_dsc_t *icon;
    void (*create)(const AppContext &context);
    void (*update)();
    void (*destroy)();
    void (*deactivate)();
};

const AppDefinition *app_find(AppId id);
const AppDefinition *app_at(size_t index);
size_t app_count();
