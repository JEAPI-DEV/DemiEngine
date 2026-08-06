#pragma once

#include "demi/runtime/scene/model/Entity.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
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
  void clear();
  [[nodiscard]] bool empty() const;

private:
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
};

} // namespace demi::runtime
