#pragma once

#include "demi/runtime/scene/model/ComponentStorage.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class Component;

struct Entity {
  std::string id;
  std::string name;
  std::unordered_map<std::string, std::string> serializedComponents;
  std::vector<std::shared_ptr<const Component>> authoredComponents;
  ComponentStorage components;

  template <typename ComponentType> [[nodiscard]] bool hasComponent() const {
    return components.contains<ComponentType>();
  }

  template <typename ComponentType> [[nodiscard]] ComponentType *component() {
    return components.get<ComponentType>();
  }

  template <typename ComponentType>
  [[nodiscard]] const ComponentType *component() const {
    return components.get<ComponentType>();
  }

  template <typename ComponentType>
  std::remove_cvref_t<ComponentType> &setComponent(ComponentType &&component) {
    return components.set(std::forward<ComponentType>(component));
  }

  template <typename ComponentType> bool removeComponent() {
    return components.remove<ComponentType>();
  }
};

[[nodiscard]] inline const std::string *
serializedComponent(const Entity &entity, const std::string &name) {
  const auto iterator = entity.serializedComponents.find(name);
  return iterator == entity.serializedComponents.end() ? nullptr
                                                       : &iterator->second;
}

} // namespace demi::runtime
