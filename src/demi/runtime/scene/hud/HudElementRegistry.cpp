#include "demi/runtime/scene/hud/HudElementRegistry.h"

#include "demi/runtime/scene/hud/elements/HudButtonElement.h"
#include "demi/runtime/scene/hud/elements/HudCircleElement.h"
#include "demi/runtime/scene/hud/elements/HudImageElement.h"
#include "demi/runtime/scene/hud/elements/HudPanelElement.h"
#include "demi/runtime/scene/hud/elements/HudRectElement.h"
#include "demi/runtime/scene/hud/elements/HudTextElement.h"

#include <array>

namespace demi::runtime::scene_loading {
namespace {
constexpr std::array Descriptors{
    makeHudElementDescriptor<HudRectElement>(),
    makeHudElementDescriptor<HudPanelElement>(),
    makeHudElementDescriptor<HudCircleElement>(),
    makeHudElementDescriptor<HudImageElement>(),
    makeHudElementDescriptor<HudTextElement>(),
    makeHudElementDescriptor<HudButtonElement>(),
};
} // namespace

std::span<const HudElementDescriptor> hudElementDescriptors() {
  return Descriptors;
}

const HudElementDescriptor *
findHudElementDescriptor(const std::string_view type) {
  for (const HudElementDescriptor &descriptor : Descriptors) {
    if (descriptor.type == type)
      return &descriptor;
  }
  return nullptr;
}
} // namespace demi::runtime::scene_loading
