#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct Entity;
struct Transform3DComponent;
struct World;

struct PhysicsRaycastHit3D {
  std::string entityId;
  Vec3 point;
  Vec3 normal;
  float distance = 0.0F;
};

[[nodiscard]] bool
collidersOverlap3D(const World &world, const Entity &left,
                   const Transform3DComponent *leftLocalOverride,
                   const Entity &right);

[[nodiscard]] std::vector<std::string>
overlapSphere3D(const World &world, Vec3 center, float radius,
                const std::string &ignoredEntityId = {});

[[nodiscard]] std::optional<PhysicsRaycastHit3D>
raycast3D(const World &world, Vec3 origin, Vec3 direction, float distance,
          const std::string &ignoredEntityId = {});

} // namespace demi::runtime
