#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime::ui {

// Adapts resolved UI nodes to the existing renderer-facing HUD value types.
// Remove this adapter when the renderers consume UiDocument directly.
void projectUiDocument(World &world);

} // namespace demi::runtime::ui
