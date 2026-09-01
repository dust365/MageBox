#pragma once
#include "apps/app_context.h"
namespace settings_app {
void create(const AppContext &context);
void update();
void destroy();
void open_wifi();
}
