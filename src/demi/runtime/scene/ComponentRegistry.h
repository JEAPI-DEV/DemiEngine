#pragma once

#include "demi/runtime/scene/Component2D.h"
#include "demi/runtime/scene/Component3D.h"
#include "demi/runtime/scene/GenericComponent.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/Entity.h"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace demi::runtime::scene_loading {

using ComponentParseFn = void (*)(const nlohmann::json &, Entity &);

struct ComponentDescriptor {
  std::string_view name;
  ComponentParseFn parse;
  bool exposedToLua = false;
  ComponentDomain domain = ComponentDomain::Generic;
};

template <typename ComponentClass>
[[nodiscard]] constexpr ComponentDescriptor makeComponentDescriptor() {
  return ComponentDescriptor{
      .name = ComponentClass::typeName,
      .parse = &ComponentClass::parse,
      .exposedToLua = ComponentClass::exposedToLua,
      .domain = ComponentClass::domain,
  };
}

[[nodiscard]] std::span<const ComponentDescriptor> componentDescriptors();
[[nodiscard]] const ComponentDescriptor *
findComponentDescriptor(std::string_view name);
[[nodiscard]] std::shared_ptr<const Component>
makeAuthoredComponent(const ComponentDescriptor &descriptor, std::string json);

} // namespace demi::runtime::scene_loading
