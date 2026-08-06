#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <set>
#include <utility>

namespace demi::runtime::render {
namespace {

constexpr Color SolidColliderColor{1.0F, 0.32F, 0.36F, 1.0F};
constexpr Color TriggerColliderColor{1.0F, 0.78F, 0.20F, 1.0F};
constexpr Color GridColor{0.20F, 0.62F, 0.62F, 0.48F};
constexpr int CircleSegments = 24;

Vec3 add(const Vec3 left, const Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(const Vec3 left, const Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 multiply(const Vec3 value, const float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Vec3 normalize(const Vec3 value) {
  const float length = std::sqrt(dot(value, value));
  return length > 0.00001F ? multiply(value, 1.0F / length) : Vec3{};
}

bool isTrigger(const Entity &entity) {
  if (const auto *collider = entity.component<BoxCollider3DComponent>())
    return collider->isTrigger;
  if (const auto *collider = entity.component<SphereCollider3DComponent>())
    return collider->isTrigger;
  if (const auto *collider = entity.component<CapsuleCollider3DComponent>())
    return collider->isTrigger;
  if (const auto *collider = entity.component<ConvexCollider3DComponent>())
    return collider->isTrigger;
  if (const auto *collider = entity.component<ModelCollider3DComponent>())
    return collider->isTrigger;
  return false;
}

void addLine(std::vector<DebugLine3D> &lines, const Vec3 start, const Vec3 end,
             const Color color) {
  lines.push_back({.start = start, .end = end, .color = color});
}

void addCircle(std::vector<DebugLine3D> &lines, const Vec3 center,
               const Vec3 firstAxis, const Vec3 secondAxis, const float radius,
               const Color color) {
  Vec3 previous = add(center, multiply(firstAxis, radius));
  for (int segment = 1; segment <= CircleSegments; ++segment) {
    const float angle = static_cast<float>(segment) /
                        static_cast<float>(CircleSegments) *
                        std::numbers::pi_v<float> * 2.0F;
    const Vec3 current =
        add(center, add(multiply(firstAxis, std::cos(angle) * radius),
                        multiply(secondAxis, std::sin(angle) * radius)));
    addLine(lines, previous, current, color);
    previous = current;
  }
}

void addBox(std::vector<DebugLine3D> &lines, const WorldTransform3D &transform,
            const BoxColliderShape3D &collider, const Color color) {
  const Vec3 half{collider.size.x * 0.5F, collider.size.y * 0.5F,
                  collider.size.z * 0.5F};
  std::array<Vec3, 8> corners;
  std::size_t index = 0;
  for (const float x : {-half.x, half.x})
    for (const float y : {-half.y, half.y})
      for (const float z : {-half.z, half.z})
        corners[index++] =
            transformPoint3D(transform, add(collider.offset, Vec3{x, y, z}));
  constexpr std::array<std::pair<int, int>, 12> Edges{{
      {0, 1},
      {0, 2},
      {0, 4},
      {1, 3},
      {1, 5},
      {2, 3},
      {2, 6},
      {3, 7},
      {4, 5},
      {4, 6},
      {5, 7},
      {6, 7},
  }};
  for (const auto [first, second] : Edges)
    addLine(lines, corners[first], corners[second], color);
}

void addSphere(std::vector<DebugLine3D> &lines,
               const WorldTransform3D &transform,
               const SphereCollider3DComponent &collider, const Color color) {
  const Vec3 center = transformPoint3D(transform, collider.offset);
  const float radius =
      collider.radius *
      std::max({std::abs(transform.scale.x), std::abs(transform.scale.y),
                std::abs(transform.scale.z)});
  addCircle(lines, center, {1, 0, 0}, {0, 1, 0}, radius, color);
  addCircle(lines, center, {1, 0, 0}, {0, 0, 1}, radius, color);
  addCircle(lines, center, {0, 1, 0}, {0, 0, 1}, radius, color);
}

void addHemisphereArc(std::vector<DebugLine3D> &lines, const Vec3 center,
                      const Vec3 axis, const Vec3 side, const float radius,
                      const float axisSign, const Color color) {
  Vec3 previous = add(center, multiply(side, -radius));
  for (int segment = 1; segment <= CircleSegments / 2; ++segment) {
    const float angle = -std::numbers::pi_v<float> * 0.5F +
                        static_cast<float>(segment) /
                            static_cast<float>(CircleSegments / 2) *
                            std::numbers::pi_v<float>;
    const Vec3 current =
        add(center, add(multiply(side, std::sin(angle) * radius),
                        multiply(axis, axisSign * std::cos(angle) * radius)));
    addLine(lines, previous, current, color);
    previous = current;
  }
}

void addCapsule(std::vector<DebugLine3D> &lines,
                const WorldTransform3D &transform,
                const CapsuleCollider3DComponent &collider, const Color color) {
  const float radius = collider.radius * std::max(std::abs(transform.scale.x),
                                                  std::abs(transform.scale.z));
  const float halfSegment = std::max(
      collider.height * std::abs(transform.scale.y) * 0.5F - radius, 0.0F);
  const Vec3 center = transformPoint3D(transform, collider.offset);
  const Vec3 axis = normalize(transformDirection3D(transform, {0, 1, 0}));
  const Vec3 first = normalize(transformDirection3D(transform, {1, 0, 0}));
  const Vec3 second = normalize(cross(axis, first));
  const Vec3 top = add(center, multiply(axis, halfSegment));
  const Vec3 bottom = subtract(center, multiply(axis, halfSegment));
  addCircle(lines, top, first, second, radius, color);
  addCircle(lines, bottom, first, second, radius, color);
  for (const Vec3 side :
       {first, multiply(first, -1.0F), second, multiply(second, -1.0F)})
    addLine(lines, add(bottom, multiply(side, radius)),
            add(top, multiply(side, radius)), color);
  addHemisphereArc(lines, top, axis, first, radius, 1.0F, color);
  addHemisphereArc(lines, top, axis, second, radius, 1.0F, color);
  addHemisphereArc(lines, bottom, axis, first, radius, -1.0F, color);
  addHemisphereArc(lines, bottom, axis, second, radius, -1.0F, color);
}

void addConvex(std::vector<DebugLine3D> &lines,
               const WorldTransform3D &transform,
               const ConvexCollider3DComponent &collider, const Color color) {
  std::vector<Vec3> points;
  points.reserve(collider.points.size());
  for (const Vec3 point : collider.points)
    points.push_back(transformPoint3D(transform, add(point, collider.offset)));
  std::set<std::pair<std::size_t, std::size_t>> edges;
  constexpr float Epsilon = 0.0001F;
  for (std::size_t first = 0; first < points.size(); ++first)
    for (std::size_t second = first + 1; second < points.size(); ++second)
      for (std::size_t third = second + 1; third < points.size(); ++third) {
        const Vec3 normal = cross(subtract(points[second], points[first]),
                                  subtract(points[third], points[first]));
        if (dot(normal, normal) <= Epsilon * Epsilon)
          continue;
        bool positive = false;
        bool negative = false;
        for (std::size_t other = 0; other < points.size(); ++other) {
          if (other == first || other == second || other == third)
            continue;
          const float side =
              dot(normal, subtract(points[other], points[first]));
          positive = positive || side > Epsilon;
          negative = negative || side < -Epsilon;
        }
        if (positive && negative)
          continue;
        edges.insert({first, second});
        edges.insert({first, third});
        edges.insert({second, third});
      }
  for (const auto [first, second] : edges)
    addLine(lines, points[first], points[second], color);
}

void addModel(std::vector<DebugLine3D> &lines,
              const WorldTransform3D &transform,
              const std::vector<TriangleCollider3D> &triangles,
              const Color color) {
  for (const TriangleCollider3D &triangle : triangles) {
    const Vec3 a = transformPoint3D(transform, triangle.a);
    const Vec3 b = transformPoint3D(transform, triangle.b);
    const Vec3 c = transformPoint3D(transform, triangle.c);
    addLine(lines, a, b, color);
    addLine(lines, b, c, color);
    addLine(lines, c, a, color);
  }
}

bool individuallyVisible(const Entity &entity) {
  if (const auto *collider = entity.component<CapsuleCollider3DComponent>())
    return collider->debugVisible;
  if (const auto *collider = entity.component<ConvexCollider3DComponent>())
    return collider->debugVisible;
  return false;
}

} // namespace

std::vector<DebugLine3D> buildDebugGeometry3D(const World &world) {
  std::vector<DebugLine3D> lines;
  if (world.debug.grid) {
    constexpr int HalfExtent = 20;
    constexpr float Height = 0.002F;
    lines.reserve(static_cast<std::size_t>((HalfExtent * 2 + 1) * 2));
    for (int coordinate = -HalfExtent; coordinate <= HalfExtent; ++coordinate) {
      addLine(lines, {static_cast<float>(coordinate), Height, -HalfExtent},
              {static_cast<float>(coordinate), Height, HalfExtent}, GridColor);
      addLine(lines, {-HalfExtent, Height, static_cast<float>(coordinate)},
              {HalfExtent, Height, static_cast<float>(coordinate)}, GridColor);
    }
  }
  for (const Entity &entity : world.entities) {
    if (!entity.enabled ||
        (!world.debug.colliders && !individuallyVisible(entity)))
      continue;
    const auto transform = resolveWorldTransform3D(world, entity);
    if (!transform)
      continue;
    const Color color =
        isTrigger(entity) ? TriggerColliderColor : SolidColliderColor;
    if (const auto *sphere = entity.component<SphereCollider3DComponent>()) {
      addSphere(lines, *transform, *sphere, color);
    } else if (const auto *capsule =
                   entity.component<CapsuleCollider3DComponent>()) {
      addCapsule(lines, *transform, *capsule, color);
    } else if (const auto *convex =
                   entity.component<ConvexCollider3DComponent>()) {
      if (convex->points.size() >= 4)
        addConvex(lines, *transform, *convex, color);
    } else if (entity.hasComponent<ModelCollider3DComponent>()) {
      if (const auto *triangles = resolvedTriangleCollider3D(world, entity))
        addModel(lines, *transform, *triangles, color);
    } else if (const auto box = resolvedBoxCollider3D(world, entity)) {
      addBox(lines, *transform, *box, color);
    }
  }
  return lines;
}

bool appendDebugGeometry3D(const World &world, PrimitiveCanvas3D &canvas) {
  for (const DebugLine3D &line : buildDebugGeometry3D(world))
    if (!canvas.line(line.start, line.end, packVertexColorRgba8(line.color)))
      return false;
  return true;
}

} // namespace demi::runtime::render
