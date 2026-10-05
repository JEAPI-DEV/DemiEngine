#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::editor {

// HUD color fields are authored as RGB or RGBA hex bytes.
std::string editorHudColorHex(const runtime::Color &color);

} // namespace demi::editor
