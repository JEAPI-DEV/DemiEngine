#include "demi/runtime/scene/Transform3DHierarchy.h"

#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {
namespace {

struct Quaternion {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float w = 1.0F;
};

Vec3 multiply(const Vec3 left, const Vec3 right) {
  return {left.x * right.x, left.y * right.y, left.z * right.z};
}

Quaternion multiply(const Quaternion left, const Quaternion right) {
  return {
      .x = left.w * right.x + left.x * right.w + left.y * right.z -
           left.z * right.y,
      .y = left.w * right.y - left.x * right.z + left.y * right.w +
           left.z * right.x,
      .z = left.w * right.z + left.x * right.y - left.y * right.x +
           left.z * right.w,
      .w = left.w * right.w - left.x * right.x - left.y * right.y -
           left.z * right.z,
  };
}

Quaternion quaternionFromEuler(const Vec3 rotation) {
  const float sx = std::sin(rotation.x * 0.5F);
  const float cx = std::cos(rotation.x * 0.5F);
  const float sy = std::sin(rotation.y * 0.5F);
  const float cy = std::cos(rotation.y * 0.5F);
  const float sz = std::sin(rotation.z * 0.5F);
  const float cz = std::cos(rotation.z * 0.5F);
  const Quaternion x{.x = sx, .w = cx};
  const Quaternion y{.y = sy, .w = cy};
  const Quaternion z{.z = sz, .w = cz};
  return multiply(z, multiply(y, x));
}

Vec3 rotate(const Quaternion rotation, const Vec3 value) {
  const Vec3 q{rotation.x, rotation.y, rotation.z};
  const Vec3 twiceCross{
      2.0F * (q.y * value.z - q.z * value.y),
      2.0F * (q.z * value.x - q.x * value.z),
      2.0F * (q.x * value.y - q.y * value.x),
  };
  const Vec3 secondCross{
      q.y * twiceCross.z - q.z * twiceCross.y,
      q.z * twiceCross.x - q.x * twiceCross.z,
      q.x * twiceCross.y - q.y * twiceCross.x,
  };
  return {value.x + rotation.w * twiceCross.x + secondCross.x,
          value.y + rotation.w * twiceCross.y + secondCross.y,
          value.z + rotation.w * twiceCross.z + secondCross.z};
}

Vec3 eulerFromQuaternion(const Quaternion value) {
  const float m00 = 1.0F - 2.0F * (value.y * value.y + value.z * value.z);
  const float m10 = 2.0F * (value.x * value.y + value.w * value.z);
  const float m20 = 2.0F * (value.x * value.z - value.w * value.y);
  const float m21 = 2.0F * (value.y * value.z + value.w * value.x);
  const float m22 = 1.0F - 2.0F * (value.x * value.x + value.y * value.y);
  return {
      .x = std::atan2(m21, m22),
      .y = std::asin(std::clamp(-m20, -1.0F, 1.0F)),
      .z = std::atan2(m10, m00),
  };
}

const Entity *findById(const World &world, const std::string &id) {
  const auto found = std::ranges::find(world.entities, id, &Entity::id);
  return found == world.entities.end() ? nullptr : &*found;
}

struct ResolvedTransform {
  WorldTransform3D transform;
  Quaternion rotation;
};

std::optional<ResolvedTransform>
resolve(const World &world, const Entity &entity,
        const Transform3DComponent &local,
        std::unordered_set<std::string> &visiting) {
  if (!visiting.insert(entity.id).second)
    return std::nullopt;

  const Quaternion localRotation = quaternionFromEuler(local.rotation);
  ResolvedTransform result{.transform = {.position = local.position,
                                         .rotation = local.rotation,
                                         .scale = local.scale},
                           .rotation = localRotation};
  if (!local.parent.empty()) {
    const Entity *parent = findById(world, local.parent);
    if (parent == nullptr || !parent->hasComponent<Transform3DComponent>())
      return std::nullopt;
    const auto parentTransform = resolve(
        world, *parent, *parent->component<Transform3DComponent>(), visiting);
    if (!parentTransform)
      return std::nullopt;
    const Vec3 offset =
        rotate(parentTransform->rotation,
               multiply(local.position, parentTransform->transform.scale));
    result.transform.position = {
        parentTransform->transform.position.x + offset.x,
        parentTransform->transform.position.y + offset.y,
        parentTransform->transform.position.z + offset.z,
    };
    result.transform.scale =
        multiply(parentTransform->transform.scale, local.scale);
    result.rotation = multiply(parentTransform->rotation, localRotation);
    result.transform.rotation = eulerFromQuaternion(result.rotation);
  }
  visiting.erase(entity.id);
  return result;
}

} // namespace

std::optional<WorldTransform3D> resolveWorldTransform3D(const World &world,
                                                        const Entity &entity) {
  const auto *local = entity.component<Transform3DComponent>();
  if (local == nullptr)
    return std::nullopt;
  return resolveWorldTransform3D(world, entity, *local);
}

std::optional<WorldTransform3D>
resolveWorldTransform3D(const World &world, const Entity &entity,
                        const Transform3DComponent &localOverride) {
  std::unordered_set<std::string> visiting;
  const auto result = resolve(world, entity, localOverride, visiting);
  return result ? std::make_optional(result->transform) : std::nullopt;
}

Vec3 transformPoint3D(const WorldTransform3D &transform,
                      const Vec3 localPoint) {
  const Vec3 offset = rotate(quaternionFromEuler(transform.rotation),
                             multiply(localPoint, transform.scale));
  return {transform.position.x + offset.x, transform.position.y + offset.y,
          transform.position.z + offset.z};
}

Vec3 transformDirection3D(const WorldTransform3D &transform,
                          const Vec3 localDirection) {
  return rotate(quaternionFromEuler(transform.rotation), localDirection);
}

std::vector<Transform3DHierarchyIssue>
validateTransform3DHierarchy(const World &world) {
  std::vector<Transform3DHierarchyIssue> issues;
  std::unordered_map<std::string, const Entity *> entities;
  for (const Entity &entity : world.entities)
    entities[entity.id] = &entity;

  for (const Entity &entity : world.entities) {
    if (!entity.hasComponent<Transform3DComponent>())
      continue;
    std::unordered_set<std::string> visiting;
    const Entity *current = &entity;
    while (current != nullptr) {
      const auto *transform = current->component<Transform3DComponent>();
      if (transform == nullptr || transform->parent.empty())
        break;
      if (!visiting.insert(current->id).second) {
        issues.push_back({.kind = Transform3DHierarchyIssueKind::Cycle,
                          .entityId = current->id,
                          .parentId = transform->parent});
        break;
      }
      const auto parent = entities.find(transform->parent);
      if (parent == entities.end() ||
          !parent->second->hasComponent<Transform3DComponent>()) {
        issues.push_back({.kind = Transform3DHierarchyIssueKind::MissingParent,
                          .entityId = current->id,
                          .parentId = transform->parent});
        break;
      }
      current = parent->second;
    }
  }
  std::ranges::sort(issues, [](const auto &left, const auto &right) {
    if (left.entityId != right.entityId)
      return left.entityId < right.entityId;
    return left.parentId < right.parentId;
  });
  return issues;
}

} // namespace demi::runtime
