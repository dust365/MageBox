#pragma once

#include "apps/app_context.h"

namespace keyboard_app {

void create(const AppContext &context);
void update();
void destroy();
void deactivate();

} // namespace keyboard_app
