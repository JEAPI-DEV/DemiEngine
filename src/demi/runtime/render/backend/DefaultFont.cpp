#include "demi/runtime/render/backend/DefaultFont.h"
#include "demi/runtime/render/InterFontData.h"

namespace demi::runtime::render {
std::span<const std::byte> defaultFontData() {
  return std::as_bytes(std::span(InterFontData));
}
const ui::FontVariations &defaultFontVariations() {
  static const ui::FontVariations axes{{"wght",500}};
  return axes;
}
}
