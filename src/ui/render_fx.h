#pragma once

#include <data_model.h>

namespace render_fx {

// Secondary page: USD/UAH 30-day chart (port of the web app's main page).
// Toggled with the white buttons; composed into the buffer, no refresh.
void render(bool hasFx, bool stale, const dash::FxData& fx);

}  // namespace render_fx
