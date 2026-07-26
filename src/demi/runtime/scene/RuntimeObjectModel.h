#pragma once

#include "demi/runtime/scene/model/World.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

struct ObjectModelResult {
  bool ok = false;
  std::string error;

  explicit operator bool() const { return ok; }
};

struct EntityQuery {
  std::vector<std::string> allComponents;
  std::vector<std::string> tags;
  std::optional<std::string> layer;
  bool includeDisabled = false;
};

class RuntimeObjectModel {
public:
  [[nodiscard]] static std::optional<Entity>
  buildEntity(const nlohmann::json &spec, std::string &error);

  [[nodiscard]] static ObjectModelResult addEntity(World &world, Entity entity,
                                                   bool replace = false);
  [[nodiscard]] static ObjectModelResult cloneEntity(
      World &world, std::string_view sourceId, std::string newId);
  [[nodiscard]] static ObjectModelResult addComponent(
      Entity &entity, std::string_view componentName,
      const nlohmann::json &values);
  [[nodiscard]] static ObjectModelResult
  removeComponent(Entity &entity, std::string_view componentName);
  [[nodiscard]] static bool hasComponent(const Entity &entity,
                                         std::string_view componentName);
  [[nodiscard]] static std::optional<nlohmann::json>
  componentField(const Entity &entity, std::string_view componentName,
                 std::string_view fieldName);
  [[nodiscard]] static ObjectModelResult
  setComponentField(Entity &entity, std::string_view componentName,
                    std::string_view fieldName,
                    const nlohmann::json &value);
  [[nodiscard]] static ObjectModelResult
  setComponentField(World &world, std::string_view entityId,
                    std::string_view componentName, std::string_view fieldName,
                    const nlohmann::json &value);
  [[nodiscard]] static std::vector<std::string>
  query(const World &world, const EntityQuery &query);

  [[nodiscard]] static ObjectModelResult
  setParent(World &world, std::string_view entityId,
            std::optional<std::string> parentId);
  [[nodiscard]] static std::optional<std::string>
  parent(const World &world, std::string_view entityId);
  [[nodiscard]] static std::vector<std::string>
  children(const World &world, std::string_view entityId);
  [[nodiscard]] static std::optional<nlohmann::json>
  localPosition(const World &world, std::string_view entityId);
  [[nodiscard]] static std::optional<nlohmann::json>
  worldPosition(const World &world, std::string_view entityId);
};

} // namespace demi::runtime
