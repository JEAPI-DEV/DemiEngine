#include "editor/EditorHudColorEncoding.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace demi::editor {

std::string editorHudColorHex(const runtime::Color &color) {
  const auto byte = [](const float value) {
    return static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  char buffer[10];
  const int red = byte(color.r);
  const int green = byte(color.g);
  const int blue = byte(color.b);
  const int alpha = byte(color.a);
  if (alpha == 255)
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", red, green, blue);
  else
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", red, green, blue,
                  alpha);
  return {buffer};
}

} // namespace demi::editor
