#pragma once
#include <cstddef>
#include <span>
#include "demi/runtime/ui/FontRasterizer.h"

namespace demi::runtime::render {
// Embedded Inter variable font, shared by runtime
// text and editor UI. Static lifetime; no dependency on installed system fonts.
[[nodiscard]] std::span<const std::byte> defaultFontData();
[[nodiscard]] const ui::FontVariations &defaultFontVariations();
}
