#pragma once

#include "demi/runtime/scene/Component2D.h"
#include "demi/runtime/scene/Component3D.h"
#include "demi/runtime/scene/GenericComponent.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/Entity.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::scene_loading {

using ComponentParseFn = void (*)(const nlohmann::json &, Entity &);
using ComponentSerializeFn = nlohmann::json (*)(const Entity &);
using ComponentDefaultsFn = nlohmann::json (*)();

struct ComponentValidationError {
  std::string field;
  std::string message;
};

struct ComponentDescriptor {
  std::string_view name;
  ComponentParseFn parse;
  ComponentSerializeFn serialize;
  ComponentDefaultsFn defaults;
  std::span<const ComponentFieldDescriptor> fields;
  ComponentEditorMetadata editor;
  bool exposedToLua = false;
  ComponentDomain domain = ComponentDomain::Generic;
};

template <typename ComponentClass>
void parseComponent(const nlohmann::json &json, Entity &entity) {
  ComponentClass::parse(json, entity);
  entity.serializedComponents.insert_or_assign(
      std::string(ComponentClass::typeName), json.dump());
}

template <typename ComponentClass>
[[nodiscard]] nlohmann::json serializeComponent(const Entity &entity) {
  const auto serialized =
      entity.serializedComponents.find(std::string(ComponentClass::typeName));
  if (serialized != entity.serializedComponents.end()) {
    return nlohmann::json::parse(serialized->second);
  }
  return nlohmann::json::object();
}

template <typename ComponentClass>
[[nodiscard]] nlohmann::json defaultComponentJson() {
  Entity entity;
  parseComponent<ComponentClass>(nlohmann::json::object(), entity);
  return serializeComponent<ComponentClass>(entity);
}

template <typename ComponentClass>
[[nodiscard]] constexpr std::span<const ComponentFieldDescriptor>
componentFields() {
  if constexpr (requires { ComponentClass::fields; }) {
    return ComponentClass::fields;
  }
  return {};
}

template <typename ComponentClass>
[[nodiscard]] constexpr ComponentEditorMetadata componentEditorMetadata() {
  if constexpr (requires { ComponentClass::editor; }) {
    return ComponentClass::editor;
  }
  return ComponentEditorMetadata{.category = "Component",
                                 .displayName = ComponentClass::typeName};
}

template <typename ComponentClass>
[[nodiscard]] constexpr ComponentDescriptor makeComponentDescriptor() {
  return ComponentDescriptor{
      .name = ComponentClass::typeName,
      .parse = &parseComponent<ComponentClass>,
      .serialize = &serializeComponent<ComponentClass>,
      .defaults = &defaultComponentJson<ComponentClass>,
      .fields = componentFields<ComponentClass>(),
      .editor = componentEditorMetadata<ComponentClass>(),
      .exposedToLua = ComponentClass::exposedToLua,
      .domain = ComponentClass::domain,
  };
}

[[nodiscard]] std::vector<ComponentValidationError>
validateComponent(const ComponentDescriptor &descriptor,
                  const nlohmann::json &json);
[[nodiscard]] nlohmann::json
componentDefaults(const ComponentDescriptor &descriptor);
[[nodiscard]] nlohmann::json
componentSchema(const ComponentDescriptor &descriptor);
[[nodiscard]] nlohmann::json canonicalComponentSchema();

[[nodiscard]] std::span<const ComponentDescriptor> componentDescriptors();
[[nodiscard]] const ComponentDescriptor *
findComponentDescriptor(std::string_view name);
[[nodiscard]] std::shared_ptr<const Component>
makeAuthoredComponent(const ComponentDescriptor &descriptor, std::string json);

} // namespace demi::runtime::scene_loading
