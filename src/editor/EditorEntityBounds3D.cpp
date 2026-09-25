#include "editor/EditorEntityBounds3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/CapsuleCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ConvexCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>

namespace demi::editor {
namespace {

runtime::Vec3 add(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

runtime::Vec3 subtract(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

EditorBounds3D orderedBounds(runtime::Vec3 minimum, runtime::Vec3 maximum) {
  if (minimum.x > maximum.x)
    std::swap(minimum.x, maximum.x);
  if (minimum.y > maximum.y)
    std::swap(minimum.y, maximum.y);
  if (minimum.z > maximum.z)
    std::swap(minimum.z, maximum.z);
  return {.minimum = minimum, .maximum = maximum};
}

void extend(EditorBounds3D &destination, const runtime::Vec3 point) {
  destination.minimum = {std::min(destination.minimum.x, point.x),
                         std::min(destination.minimum.y, point.y),
                         std::min(destination.minimum.z, point.z)};
  destination.maximum = {std::max(destination.maximum.x, point.x),
                         std::max(destination.maximum.y, point.y),
                         std::max(destination.maximum.z, point.z)};
}

void extend(EditorBounds3D &destination, const EditorBounds3D &source) {
  extend(destination, source.minimum);
  extend(destination, source.maximum);
}

} // namespace

std::optional<EditorEntityBounds3D>
editorEntityBounds3D(const runtime::World &world,
                     const runtime::Entity &entity) {
  const auto transform = runtime::resolveWorldTransform3D(world, entity);
  if (!transform)
    return std::nullopt;

  runtime::Vec3 minimum{-0.2F, -0.2F, -0.2F};
  runtime::Vec3 maximum{0.2F, 0.2F, 0.2F};
  bool isSceneGeometry = false;
  const auto *instances = entity.component<runtime::MeshInstances3DComponent>();
  if (const auto box = runtime::resolvedBoxCollider3D(world, entity);
      instances == nullptr && box) {
    const runtime::Vec3 half{box->size.x * 0.5F, box->size.y * 0.5F,
                             box->size.z * 0.5F};
    minimum = subtract(box->offset, half);
    maximum = add(box->offset, half);
    isSceneGeometry = true;
  } else if (const auto *sphere =
                 entity.component<runtime::SphereCollider3DComponent>();
             instances == nullptr && sphere != nullptr) {
    const runtime::Vec3 radius{sphere->radius, sphere->radius, sphere->radius};
    minimum = subtract(sphere->offset, radius);
    maximum = add(sphere->offset, radius);
    isSceneGeometry = true;
  } else if (const auto *capsule =
                 entity.component<runtime::CapsuleCollider3DComponent>();
             instances == nullptr && capsule != nullptr) {
    const runtime::Vec3 half{capsule->radius, capsule->height * 0.5F,
                             capsule->radius};
    minimum = subtract(capsule->offset, half);
    maximum = add(capsule->offset, half);
    isSceneGeometry = true;
  } else if (const auto *convex =
                 entity.component<runtime::ConvexCollider3DComponent>();
             instances == nullptr && convex != nullptr &&
             !convex->points.empty()) {
    minimum = add(convex->points.front(), convex->offset);
    maximum = minimum;
    for (const runtime::Vec3 point : convex->points) {
      const runtime::Vec3 value = add(point, convex->offset);
      minimum = {std::min(minimum.x, value.x),
                 std::min(minimum.y, value.y),
                 std::min(minimum.z, value.z)};
      maximum = {std::max(maximum.x, value.x),
                 std::max(maximum.y, value.y),
                 std::max(maximum.z, value.z)};
    }
    isSceneGeometry = true;
  } else if (const auto *mesh =
                 entity.component<runtime::MeshRendererComponent>()) {
    minimum = mesh->hasBounds ? mesh->boundsMin
                              : runtime::Vec3{-0.5F, -0.5F, -0.5F};
    maximum = mesh->hasBounds ? mesh->boundsMax
                              : runtime::Vec3{0.5F, 0.5F, 0.5F};
    minimum = {minimum.x * mesh->size.x, minimum.y * mesh->size.y,
               minimum.z * mesh->size.z};
    maximum = {maximum.x * mesh->size.x, maximum.y * mesh->size.y,
               maximum.z * mesh->size.z};
    isSceneGeometry = true;
  }

  EditorEntityBounds3D result{.local = orderedBounds(minimum, maximum),
                              .isSceneGeometry = isSceneGeometry};
  if (instances != nullptr) {
    result.worldTransforms.reserve(instances->transforms.size());
    for (const auto &[instanceId, localTransform] : instances->transforms) {
      (void)instanceId;
      result.worldTransforms.push_back(
          runtime::composeWorldTransform3D(*transform, localTransform));
    }
  } else {
    result.worldTransforms.push_back(*transform);
  }
  return result;
}

std::optional<EditorBounds3D>
editorWorldBounds3D(const EditorEntityBounds3D &entityBounds) {
  std::optional<EditorBounds3D> result;
  for (const runtime::WorldTransform3D &transform :
       entityBounds.worldTransforms) {
    for (const float x :
         {entityBounds.local.minimum.x, entityBounds.local.maximum.x}) {
      for (const float y :
           {entityBounds.local.minimum.y, entityBounds.local.maximum.y}) {
        for (const float z :
             {entityBounds.local.minimum.z, entityBounds.local.maximum.z}) {
          const runtime::Vec3 point =
              runtime::transformPoint3D(transform, {x, y, z});
          if (result)
            extend(*result, point);
          else
            result = EditorBounds3D{.minimum = point, .maximum = point};
        }
      }
    }
  }
  return result;
}

std::optional<EditorBounds3D>
editorSceneBounds3D(const runtime::World &world) {
  std::optional<EditorBounds3D> result;
  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    const auto entityBounds = editorEntityBounds3D(world, entity);
    if (!entityBounds || !entityBounds->isSceneGeometry)
      continue;
    const auto worldBounds = editorWorldBounds3D(*entityBounds);
    if (!worldBounds)
      continue;
    if (result)
      extend(*result, *worldBounds);
    else
      result = *worldBounds;
  }
  return result;
}

} // namespace demi::editor
