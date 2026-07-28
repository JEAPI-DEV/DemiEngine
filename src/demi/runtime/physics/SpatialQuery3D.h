#pragma once

#include "demi/runtime/physics/Physics3DTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct Entity;
struct Transform3DComponent;
struct World;

[[nodiscard]] bool
collidersOverlap3D(const World &world, const Entity &left,
                   const Transform3DComponent *leftLocalOverride,
                   const Entity &right);

[[nodiscard]] std::vector<std::string>
overlapSphere3D(const World &world, Vec3 center, float radius,
                const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsQueryHit3D>
overlapSphereAll3D(const World &world, Vec3 center, float radius,
                   const std::string &layer = {},
                   const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsQueryHit3D>
overlapBoxAll3D(const World &world, Vec3 center, Vec3 size,
                const std::string &layer = {},
                const std::string &ignoredEntityId = {});
[[nodiscard]] std::vector<PhysicsQueryHit3D>
overlapCapsuleAll3D(const World &world, Vec3 center, float radius,
                    float height, const std::string &layer = {},
                    const std::string &ignoredEntityId = {});

[[nodiscard]] std::optional<PhysicsRaycastHit3D>
raycast3D(const World &world, Vec3 origin, Vec3 direction, float distance,
          const std::string &ignoredEntityId = {});
[[nodiscard]] std::optional<PhysicsQueryHit3D>
sphereCast3D(const World &world, Vec3 origin, float radius, Vec3 direction,
             float distance, const std::string &layer = {},
             const std::string &ignoredEntityId = {});
[[nodiscard]] std::optional<PhysicsQueryHit3D>
capsuleCast3D(const World &world, Vec3 origin, float radius, float height,
              Vec3 direction, float distance, const std::string &layer = {},
              const std::string &ignoredEntityId = {});

} // namespace demi::runtime
