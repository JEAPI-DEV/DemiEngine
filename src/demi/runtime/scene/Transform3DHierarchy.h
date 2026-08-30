#pragma once

#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct Entity;
struct World;

struct WorldTransform3D {
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
};

enum class Transform3DHierarchyIssueKind {
  MissingParent,
  Cycle,
};

struct Transform3DHierarchyIssue {
  Transform3DHierarchyIssueKind kind;
  std::string entityId;
  std::string parentId;
};

[[nodiscard]] std::optional<WorldTransform3D>
resolveWorldTransform3D(const World &world, const Entity &entity);

[[nodiscard]] std::optional<WorldTransform3D>
resolveWorldTransform3D(const World &world, const Entity &entity,
                        const Transform3DComponent &localOverride);

[[nodiscard]] Vec3 transformPoint3D(const WorldTransform3D &transform,
                                    Vec3 localPoint);

[[nodiscard]] Vec3 transformDirection3D(const WorldTransform3D &transform,
                                        Vec3 localDirection);
[[nodiscard]] Vec3 inverseTransformPoint3D(const WorldTransform3D &transform,
                                           Vec3 worldPoint);
[[nodiscard]] Vec3 inverseTransformVector3D(const WorldTransform3D &transform,
                                            Vec3 worldVector);
[[nodiscard]] Vec3 rotateLocalEuler3D(Vec3 rotation, Vec3 localAxis,
                                      float radians);
[[nodiscard]] Vec3 rotateWorldEuler3D(Vec3 rotation, Vec3 worldAxis,
                                      float radians);
[[nodiscard]] std::optional<Transform3DComponent>
worldToLocalTransform3D(const World &world, const Entity &entity,
                        const WorldTransform3D &worldTransform);
[[nodiscard]] Vec3 forwardDirection3D(const WorldTransform3D &transform);
[[nodiscard]] Vec3 rightDirection3D(const WorldTransform3D &transform);
[[nodiscard]] Vec3 upDirection3D(const WorldTransform3D &transform);
[[nodiscard]] Vec3 lookAtRotation3D(Vec3 origin, Vec3 target);

[[nodiscard]] std::vector<Transform3DHierarchyIssue>
validateTransform3DHierarchy(const World &world);

} // namespace demi::runtime
