#include "demi/runtime/ui/UiImageSkin.h"
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace demi::runtime::ui {
std::optional<UiImageSkin> parseUiImageSkin(const nlohmann::json &node) {
  UiImageSkin result;
  const auto finiteNonnegative = [](const nlohmann::json &value) {
    return value.is_number() && std::isfinite(value.get<double>()) &&
           value.get<double>() >= 0 &&
           value.get<double>() <= std::numeric_limits<float>::max();
  };
  if (node.contains("nine_slice")) {
    const auto &value = node["nine_slice"];
    if (!value.is_array() || value.size() != 2) {
      return std::nullopt;
    }
    for (const auto &pair : value) {
      if (!pair.is_array() || pair.size() != 2 || !finiteNonnegative(pair[0]) ||
          !finiteNonnegative(pair[1])) {
        return std::nullopt;
      }
    }
    result.start = {value[0][0].get<float>(), value[0][1].get<float>()};
    result.end = {value[1][0].get<float>(), value[1][1].get<float>()};
  }
  if (node.contains("nine_slice_scale")) {
    const auto &scale = node["nine_slice_scale"];
    if (!finiteNonnegative(scale) || scale.get<double>() <= 0) {
      return std::nullopt;
    }
    result.scale = scale.get<float>();
  }
  if (result.sliced() && node.contains("source_size")) {
    const auto &size = node["source_size"];
    if (!size.is_array() || size.size() != 2 || !finiteNonnegative(size[0]) ||
        !finiteNonnegative(size[1])) {
      return std::nullopt;
    }
    if ((size[0].get<float>() > 0 &&
         result.start.x + result.end.x > size[0].get<float>()) ||
        (size[1].get<float>() > 0 &&
         result.start.y + result.end.y > size[1].get<float>())) {
      return std::nullopt;
    }
  }
  return result;
}
} // namespace demi::runtime::ui
