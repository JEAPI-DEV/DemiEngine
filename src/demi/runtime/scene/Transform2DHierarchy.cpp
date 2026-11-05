#include "demi/runtime/scene/Transform2DHierarchy.h"

#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace demi::runtime {
namespace {
Vec2 rotate(Vec2 point, float rotation) {
  const float cosine = std::cos(rotation);
  const float sine = std::sin(rotation);
  return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine};
}
WorldTransform2D localPose(const Transform2DComponent &transform) {
  return {transform.position, transform.rotation, transform.scale};
}
} // namespace

WorldTransform2D composeWorldTransform2D(const WorldTransform2D &parent,
                                         const WorldTransform2D &local) {
  return {.position = transformPoint2D(parent, local.position),
          .rotation = parent.rotation + local.rotation,
          .scale = {parent.scale.x * local.scale.x,
                    parent.scale.y * local.scale.y}};
}

Vec2 transformPoint2D(const WorldTransform2D &transform, Vec2 point) {
  const Vec2 offset =
      rotate({point.x * transform.scale.x, point.y * transform.scale.y},
             transform.rotation);
  return {transform.position.x + offset.x, transform.position.y + offset.y};
}

Vec2 inverseTransformPoint2D(const WorldTransform2D &transform, Vec2 point) {
  const Vec2 offset =
      rotate({point.x - transform.position.x, point.y - transform.position.y},
             -transform.rotation);
  return {transform.scale.x != 0.0F ? offset.x / transform.scale.x : 0.0F,
          transform.scale.y != 0.0F ? offset.y / transform.scale.y : 0.0F};
}

std::optional<WorldTransform2D> resolveWorldTransform2D(const World &world,
                                                        const Entity &entity) {
  const auto *transform = entity.component<Transform2DComponent>();
  if (!transform) {
    return std::nullopt;
  }
  if (transform->parent.empty()) {
    return localPose(*transform);
  }
  std::vector<const Entity *> chain;
  const Entity *current = &entity;
  while (current && current->hasComponent<Transform2DComponent>()) {
    if (std::ranges::find(chain, current) != chain.end()) {
      return std::nullopt;
    }
    chain.push_back(current);
    const auto &parent = current->component<Transform2DComponent>()->parent;
    if (parent.empty()) {
      break;
    }
    const auto found = std::ranges::find(world.entities, parent, &Entity::id);
    current = found == world.entities.end() ? nullptr : &*found;
  }
  WorldTransform2D result;
  for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator) {
    result = composeWorldTransform2D(
        result, localPose(*(*iterator)->component<Transform2DComponent>()));
  }
  return result;
}

std::vector<std::optional<WorldTransform2D>> resolveWorldTransforms2D(
    const World &world,
    std::span<const std::optional<WorldTransform2D>> worldOverrides) {
  std::vector<std::optional<WorldTransform2D>> results(world.entities.size());
  std::vector<unsigned char> states(world.entities.size(), 0);
  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(world.entities.size());
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    indices.emplace(world.entities[index].id, index);
  }
  const auto resolve =
      [&](auto &&self, std::size_t index) -> std::optional<WorldTransform2D> {
    if (states[index] == 2) {
      return results[index];
    }
    if (states[index] == 1) {
      return std::nullopt;
    }
    states[index] = 1;
    const auto *transform =
        world.entities[index].component<Transform2DComponent>();
    if (index < worldOverrides.size() && worldOverrides[index]) {
      results[index] = worldOverrides[index];
    } else if (transform) {
      auto pose = localPose(*transform);
      const auto parent = indices.find(transform->parent);
      if (!transform->parent.empty() && parent != indices.end() &&
          world.entities[parent->second].hasComponent<Transform2DComponent>()) {
        if (const auto parentPose = self(self, parent->second)) {
          results[index] = composeWorldTransform2D(*parentPose, pose);
        }
      } else {
        results[index] = pose;
      }
    }
    states[index] = 2;
    return results[index];
  };
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    resolve(resolve, index);
  }
  return results;
}

Transform2DComponent
localTransform2D(const Transform2DComponent &authored,
                 const WorldTransform2D &pose,
                 const std::optional<WorldTransform2D> &parentPose) {
  auto result = authored;
  result.position = parentPose
                        ? inverseTransformPoint2D(*parentPose, pose.position)
                        : pose.position;
  result.rotation = pose.rotation - (parentPose ? parentPose->rotation : 0.0F);
  // Physics only publishes position and rotation; authored scale remains local.
  return result;
}
} // namespace demi::runtime
