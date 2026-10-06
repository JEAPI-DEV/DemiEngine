#pragma once

#include "demi/runtime/scene/model/Entity.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace demi::runtime {

struct World;

enum class WorldMutationKind {
  Created,
  Replaced,
  Destroyed,
  ComponentAdded,
  ComponentRemoved,
  EnabledChanged,
};

struct WorldMutation {
  WorldMutationKind kind;
  std::string entityId;
  std::string component;
};

class WorldCommandBuffer {
public:
  [[nodiscard]] bool create(const World &world, Entity entity,
                            bool replace = false);
  [[nodiscard]] bool clone(const World &world, std::string_view sourceId,
                           std::string newId);
  [[nodiscard]] bool destroy(const World &world, std::string entityId);
  [[nodiscard]] bool addComponent(const World &world, std::string entityId,
                                  std::string component,
                                  nlohmann::json values);
  [[nodiscard]] bool removeComponent(const World &world, std::string entityId,
                                     std::string component);
  [[nodiscard]] bool setEnabled(const World &world, std::string entityId,
                                bool enabled);
  [[nodiscard]] std::vector<WorldMutation> flush(World &world);
  [[nodiscard]] Entity *pendingEntity(std::string_view id);
  [[nodiscard]] const Entity *pendingEntity(std::string_view id) const;
  // Stable IDs referenced by queued commands, for narrow transactional staging.
  [[nodiscard]] std::vector<std::string> affectedEntityIds() const;
  void clear();
  [[nodiscard]] bool empty() const;

private:
  struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view>{}(value);
    }
  };
  using EntityIndex =
      std::unordered_map<std::string, std::size_t, StringHash,
                         std::equal_to<>>;

  struct Create {
    Entity entity;
    bool replace = false;
  };
  struct Destroy {
    std::string id;
  };
  struct AddComponent {
    std::string id;
    std::string component;
    nlohmann::json values;
  };
  struct RemoveComponent {
    std::string id;
    std::string component;
  };
  struct SetEnabled {
    std::string id;
    bool enabled = true;
  };
  using Command =
      std::variant<Create, Destroy, AddComponent, RemoveComponent, SetEnabled>;
  std::vector<Command> commands_;
  // Indices remain valid across vector reallocations; cleared with commands_.
  EntityIndex pendingCreates_;
};

} // namespace demi::runtime
