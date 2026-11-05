#include "demi/runtime/physics/RadialRaycast2D.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/visibility/VisibilityQueries2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <cmath>
#include <numbers>
#include <limits>

using namespace demi::runtime;

namespace {

void addWall(World &world, Vec2 center, Vec2 size = {1, 1}) {
  Entity wall;
  wall.id = "wall_" + std::to_string(world.entities.size());
  wall.setComponent(Transform2DComponent{.position = center});
  wall.setComponent(BoxCollider2DComponent{.size = size, .layer = "wall"});
  world.entities.push_back(std::move(wall));
}

float polygonDistance(const VisibilityBoundary2D &boundary, float angle) {
  const auto upper = std::upper_bound(boundary.angles.begin(),
                                       boundary.angles.end(), angle);
  const std::size_t next =
      static_cast<std::size_t>(upper - boundary.angles.begin()) %
      boundary.angles.size();
  const std::size_t previous =
      (next + boundary.angles.size() - 1) % boundary.angles.size();
  // Measure sampling error without adding float cross-product cancellation
  // to the geometry under test, particularly at the maximum sight radius.
  struct Point {
    double x;
    double y;
  };
  const auto point = [&](std::size_t index) {
    const double angle = boundary.angles[index];
    return Point{boundary.distances[index] * std::cos(angle),
                  boundary.distances[index] * std::sin(angle)};
  };
  const Point a = point(previous);
  const Point b = point(next);
  const Point direction{std::cos(static_cast<double>(angle)),
                         std::sin(static_cast<double>(angle))};
  const double determinant =
      direction.x * (b.y - a.y) - direction.y * (b.x - a.x);
  if (std::abs(determinant) < 0.00000001F) {
    return std::min(boundary.distances[previous], boundary.distances[next]);
  }
  return (a.x * b.y - a.y * b.x) / determinant;
}

void checkRangeAndCurveError() {
  constexpr float tau = 2 * std::numbers::pi_v<float>;
  World empty;
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::resetSession();
  RuntimeProfiler::beginFrame();
  for (const float radius : {.1F, 1.0F, 11.0F, 256.0F, 1000.0F}) {
    const auto boundary = castVisibilityBoundary2D(empty, {}, radius, "wall");
    assert(boundary.angles.size() >= 32 && boundary.angles.size() <= 512);
    if (radius == 11) {
      assert(boundary.angles.size() < 128);
      const auto entries = RuntimeProfiler::frameEntries();
      const auto rays = std::ranges::find(entries, "Visibility.rays_cast",
                                           &RuntimeProfiler::Entry::name);
      const auto points = std::ranges::find(entries, "Visibility.boundary_points",
                                             &RuntimeProfiler::Entry::name);
      assert(rays != entries.end() && points != entries.end());
      assert(rays->gauge == boundary.angles.size());
      assert(points->gauge == boundary.angles.size());
    }
    if (radius == 256) {
      assert(boundary.angles.size() == 512);
    }
    const float tolerance = radius <= 256
        ? .005F
        : radius * (1 - std::cos(std::numbers::pi_v<float> / 512));
    for (int index = 0; index < 4096; ++index) {
      const float distance = polygonDistance(boundary, (index + .5F) * tau / 4096);
      assert(distance <= radius + .0001F);
      if (radius - distance > tolerance + .0002F) {
        std::cerr << "Range chord error: radius " << radius << " error "
                  << radius - distance << " bound " << tolerance << '\n';
      }
      assert(radius - distance <= tolerance + .0002F);
    }
  }
  RuntimeProfiler::setEnabled(false);
  for (const float radius : {0.0F, -1.0F,
                             std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
    assert(castVisibilityBoundary2D(empty, {}, radius, "wall").angles.empty());
  }
  assert(castVisibilityBoundary2D(empty, {},
      std::numeric_limits<float>::max(), "wall").angles.size() == 512);
  for (const auto [center, radius] : {
           std::pair{Vec2{3.4F, .3F}, .8F},
           std::pair{Vec2{6.2F, .1F}, .1F},
           std::pair{Vec2{1.2F, .4F}, .8F}}) {
    World world;
    Entity circle;
    circle.id = "curve";
    circle.setComponent(Transform2DComponent{.position = center});
    circle.setComponent(CircleCollider2DComponent{.radius = radius, .layer = "wall"});
    world.entities.push_back(std::move(circle));
    const auto boundary = castVisibilityBoundary2D(world, {}, 11, "wall");
    assert(boundary.angles.size() <= 4096);
    const RadialRaycast2D exact(world, {}, 11, "wall");
    for (int index = 0; index < 8192; ++index) {
      const float angle = (index + .5F) * tau / 8192;
      const auto hit = exact.cast({std::cos(angle), std::sin(angle)});
      if (hit) {
        const float distance = polygonDistance(boundary, angle);
        if (distance > hit->distance + .003F) {
          std::cerr << "Curve chord error: center " << center.x << ',' << center.y
                    << " radius " << radius << " angle " << angle
                    << " polygon " << distance << " native " << hit->distance << '\n';
        }
        assert(distance <= hit->distance + .003F);
      }
    }
  }
}

void checkInteriorBlocker() {
  constexpr float angle = std::numbers::pi_v<float> / 512;
  for (const bool circular : {false, true}) {
    World world;
    Entity blocker;
    blocker.id = "thin_blocker";
    blocker.setComponent(Transform2DComponent{
        .position = {5 * std::cos(angle), 5 * std::sin(angle)},
        .rotation = .4F,
        .scale = {-1.2F, .8F}});
    if (circular) {
      blocker.setComponent(CircleCollider2DComponent{.radius = .001F,
                                                     .layer = "wall"});
    } else {
      blocker.setComponent(BoxCollider2DComponent{.size = {.002F, .002F},
                                                  .layer = "wall"});
    }
    world.entities.push_back(std::move(blocker));
    const auto boundary = castVisibilityBoundary2D(world, {}, 11, "wall");
    const auto exact = RadialRaycast2D(world, {}, 11, "wall").cast(
        {std::cos(angle), std::sin(angle)});
    assert(exact);
    assert(polygonDistance(boundary, angle) <= exact->distance + .002F);
    world.entities.front().enabled = false;
    assert(RadialRaycast2D(world, {}, 11, "wall").silhouetteAngles().empty());
    world.entities.front().enabled = true;
    assert(RadialRaycast2D(world, {}, 11, "wall", "thin_blocker")
               .silhouetteAngles().empty());
  }
}

void checkCorridorSeams() {
  World corridor;
  // Real floor-one wall cells around the Watch/Cloister lower corridor.
  for (int x : {24, 25, 26, 27, 31, 32, 33, 35}) {
    addWall(corridor, {float(x), 14});
  }
  for (int y : {15, 16, 17}) {
    addWall(corridor, {33, float(y)});
    addWall(corridor, {35, float(y)});
  }
  for (const Vec2 focus : {Vec2{29, 14.5F}, Vec2{29, 15}, Vec2{29, 15.5F},
                           Vec2{29, 14.499F}, Vec2{29, 14.501F}}) {
    const auto boundary = castVisibilityBoundary2D(corridor, focus, 11, "wall");
    assert(boundary.angles.size() <= 4096);
    const RadialRaycast2D exact(corridor, focus, 11, "wall");
    const auto check = [&](float angle) {
      const auto hit = exact.cast({std::cos(angle), std::sin(angle)});
      const float distance = hit ? hit->distance : 11;
      const float reconstructed = polygonDistance(boundary, angle);
      if (reconstructed > distance + 0.02F) {
        std::cerr << "Corridor boundary leak: focus " << focus.x << ',' << focus.y
                  << " angle " << angle << " polygon " << reconstructed
                  << " exact " << distance << '\n';
      }
      assert(reconstructed <= distance + 0.02F);
    };
    constexpr float tau = 2 * std::numbers::pi_v<float>;
    for (int index = 0; index < 4096; ++index) {
      check((index + 0.5F) * tau / 4096);
    }
    for (const float angle : {0.0F, 0.000001F, 0.0001F,
                              tau - 0.000001F, tau - 0.0001F}) {
      check(angle);
    }
  }
}

} // namespace

int main() {
  checkRangeAndCurveError();
  checkCorridorSeams();
  checkInteriorBlocker();
  World world;
  Entity pillar;
  pillar.id = "pillar";
  pillar.setComponent(Transform2DComponent{.position = {1, 3}});
  pillar.setComponent(BoxCollider2DComponent{.size = {2, 2}, .layer = "wall"});
  world.entities.push_back(std::move(pillar));
  const auto boundary = castVisibilityBoundary2D(world, {}, 11, "wall");
  const float corner = std::numbers::pi_v<float> / 4;
  bool beforeCorner = false;
  bool afterCorner = false;
  for (std::size_t index = 0; index < boundary.angles.size(); ++index) {
    const float offset = boundary.angles[index] - corner;
    beforeCorner |=
        offset < 0 && offset > -0.00002F && boundary.distances[index] > 10;
    afterCorner |=
        offset >= 0 && offset < 0.00002F && boundary.distances[index] < 3;
  }
  assert(beforeCorner && afterCorner);
  // Moving the observer changes corner angles continuously, including angles
  // between the original uniformly sampled rays.
  for (float x = -0.2F; x <= 0.2F; x += 0.05F) {
    const Vec2 focus{x, 0.13F};
    const auto moved = castVisibilityBoundary2D(world, focus, 11, "wall");
    const float angle = std::atan2(2 - focus.y, 2 - focus.x);
    bool foreground = false;
    bool background = false;
    for (std::size_t index = 0; index < moved.angles.size(); ++index) {
      const float offset = moved.angles[index] - angle;
      foreground |=
          offset >= 0 && offset < 0.00002F && moved.distances[index] < 4;
      background |=
          offset < 0 && offset > -0.00002F && moved.distances[index] > 10;
    }
    assert(foreground && background);
  }
  Entity rotated;
  rotated.id = "rotated";
  rotated.setComponent(Transform2DComponent{
      .position = {-3, 1}, .rotation = 0.7F, .scale = {-1.2F, 0.8F}});
  rotated.setComponent(BoxCollider2DComponent{
      .size = {2, 0.5F}, .offset = {0.3F, -0.4F}, .layer = "wall"});
  world.entities.push_back(std::move(rotated));
  Entity circle;
  circle.id = "circle";
  circle.setComponent(
      Transform2DComponent{.position = {3, -2}, .scale = {1.5F, 0.8F}});
  circle.setComponent(CircleCollider2DComponent{
      .radius = 0.8F, .offset = {0.2F, 0.2F}, .layer = "wall"});
  world.entities.push_back(std::move(circle));
  for (Vec2 origin : {Vec2{0, 0}, Vec2{2, 4}, Vec2{-1, 1}}) {
    const RadialRaycast2D batch(world, origin, 11, "wall");
    for (int step = 0; step < 256; ++step) {
      const float angle = step * 2 * std::numbers::pi_v<float> / 256;
      const Vec2 direction{std::cos(angle), std::sin(angle)};
      const auto expected = raycast2D(world, origin, direction, 11, "wall");
      const auto actual = batch.cast(direction);
      assert(expected.has_value() == actual.has_value());
      if (expected) {
        assert(expected->entityId == actual->entityId);
        assert(std::abs(expected->distance - actual->distance) < 0.00001F);
        assert(std::abs(expected->normal.x - actual->normal.x) < 0.00001F);
        assert(std::abs(expected->normal.y - actual->normal.y) < 0.00001F);
      }
    }
  }
  const std::vector<PhysicsRay2D> rays{
      {{0, 1}, 1}, {{0, 1}, 8}, {{1, 0}, 0}, {{1, -1}, 10}};
  for (const std::string ignored : {std::string{}, std::string{"pillar"}}) {
    const auto batch = raycastBatch2D(world, {}, rays, "wall", ignored);
    assert(batch.error.empty() && batch.hits.size() == rays.size());
    for (std::size_t index = 0; index < rays.size(); ++index) {
      const auto single = raycast2D(world, {}, rays[index].direction,
                                    rays[index].distance, "wall", ignored);
      assert(single.has_value() == batch.hits[index].has_value());
      if (single) {
        assert(single->entityId == batch.hits[index]->entityId);
        assert(single->fraction == batch.hits[index]->fraction);
      }
    }
  }
  auto invalid = rays;
  invalid[0].distance = -1;
  assert(!raycastBatch2D(world, {}, invalid, "wall").error.empty());
  assert(raycastBatch2D(world, {}, {}, "wall").hits.empty());
  world.entities[1].enabled = false;
  world.entities[2].enabled = false;
  world.entities[0].enabled = false;
  const auto unobstructed = castVisibilityBoundary2D(world, {}, 11, "wall");
  for (float distance : unobstructed.distances) {
    assert(distance == 11);
  }
}
