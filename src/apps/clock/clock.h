#pragma once

#include "apps/app_context.h"

namespace clock_app {
void create(const AppContext &context);
void update();
void destroy();
void deactivate();
}
