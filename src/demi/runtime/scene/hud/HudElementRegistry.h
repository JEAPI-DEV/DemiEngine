#pragma once

#include "demi/runtime/scene/hud/HudElementDefinition.h"

#include <span>
#include <string>
#include <string_view>

namespace demi::runtime::scene_loading {

using HudElementParseFn = void (*)(const nlohmann::json &, const std::string &,
                                   World &);

struct HudElementDescriptor {
  std::string_view type;
  HudElementParseFn parse;
};

template <typename ElementClass>
[[nodiscard]] constexpr HudElementDescriptor makeHudElementDescriptor() {
  return {.type = ElementClass::typeName, .parse = &ElementClass::parse};
}

[[nodiscard]] std::span<const HudElementDescriptor> hudElementDescriptors();
[[nodiscard]] const HudElementDescriptor *
findHudElementDescriptor(std::string_view type);

} // namespace demi::runtime::scene_loading
