#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainSurface.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

using namespace demi::runtime;

namespace {
struct Vector {
  double x, y, z;
};
Vector subtract(Vector a, Vector b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double dot(Vector a, Vector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector cross(Vector a, Vector b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Exhaustive reference for correctness and brush-query cost comparison.
std::optional<Vec3> referenceHit(const HeightField &field, Vec3 origin,
                                 Vec3 direction) {
  double nearest = std::numeric_limits<double>::infinity();
  const auto vertex = [&](int x, int z) {
    auto position = field.position(x, z);
    return Vector{position.x, field.height(x, z), position.y};
  };
  const auto triangle = [&](Vector a, Vector b, Vector c) {
    const auto first = subtract(b, a);
    const auto second = subtract(c, a);
    const auto normal = cross(first, second);
    const double denominator =
        dot(normal, {direction.x, direction.y, direction.z});
    if (std::abs(denominator) < 1e-9)
      return;
    const double distance =
        dot(normal, subtract(a, {origin.x, origin.y, origin.z})) / denominator;
    if (distance < 0 || distance >= nearest)
      return;
    const Vector offset{origin.x + distance * direction.x - a.x,
                        origin.y + distance * direction.y - a.y,
                        origin.z + distance * direction.z - a.z};
    const double aa = dot(first, first), ab = dot(first, second),
                 bb = dot(second, second);
    const double ap = dot(first, offset), bp = dot(second, offset);
    const double divisor = aa * bb - ab * ab;
    const double u = (bb * ap - ab * bp) / divisor;
    const double v = (aa * bp - ab * ap) / divisor;
    if (u >= -1e-6 && v >= -1e-6 && u + v <= 1 + 1e-6)
      nearest = distance;
  };
  for (int z = 0; z < field.cellsZ; ++z) {
    for (int x = 0; x < field.cellsX; ++x) {
      const auto a = vertex(x, z), b = vertex(x + 1, z);
      const auto c = vertex(x, z + 1), d = vertex(x + 1, z + 1);
      triangle(a, c, b);
      triangle(b, c, d);
    }
  }
  if (!std::isfinite(nearest))
    return std::nullopt;
  return Vec3{static_cast<float>(origin.x + nearest * direction.x),
              static_cast<float>(origin.y + nearest * direction.y),
              static_cast<float>(origin.z + nearest * direction.z)};
}
} // namespace

int main() {
  // Mesh vertices are float positions, not an ideal double-precision grid.
  HeightField steep;
  steep.size = {10, 10};
  steep.cellsX = 3;
  steep.cellsZ = 1;
  steep.heights = {-100000000, 100000000, 100000000, 100000000,
                   -100000000, 100000000, 100000000, 100000000};
  const float midpoint = steep.position(1, 0).x * .5F;
  const auto steepSample = sampleTerrainHeight(steep, {midpoint, 0});
  assert(steepSample && std::abs(*steepSample) < .01F);
  const auto steepHit =
      raycastTerrain(steep, {midpoint, 200000000, 0}, {0, -1, 0});
  assert(steepHit && std::abs(steepHit->y) < .01F);
  HeightField invalid = steep;
  invalid.heights.resize(1);
  assert(!sampleTerrainHeight(invalid, {0, 0}));
  assert(!raycastTerrain(invalid, {0, 10, 0}, {0, -1, 0}));

  TerrainRecipe recipe;
  recipe.cellsX = 128;
  recipe.cellsZ = 96;
  const auto field = TerrainGenerator::generate(recipe);
  assert(field);
  const auto json = recipe.toJson();
  auto shared = std::make_shared<const HeightField>(*field);
  publishTerrain(json, shared);
  assert(acquireTerrain(json) == shared);
  shared.reset();
  assert(!findTerrain(json));

  std::mt19937 random(91);
  std::uniform_real_distribution<float> coordinate(-30, 160);
  std::uniform_real_distribution<float> offset(-1, 1);
  double exhaustiveMs = 0, gridMs = 0;
  for (int index = 0; index < 250; ++index) {
    Vec3 origin{coordinate(random), 50, coordinate(random)};
    Vec3 direction{offset(random), -1, offset(random)};
    if (index < 9) {
      origin = {float(index % 3) * 64, 50, float(index / 3) * 64};
      direction = {0, -2, 0};
    }
    const auto begin = std::chrono::steady_clock::now();
    const auto expected = referenceHit(*field, origin, direction);
    const auto middle = std::chrono::steady_clock::now();
    const auto actual = raycastTerrain(*field, origin, direction);
    const auto end = std::chrono::steady_clock::now();
    exhaustiveMs +=
        std::chrono::duration<double, std::milli>(middle - begin).count();
    gridMs += std::chrono::duration<double, std::milli>(end - middle).count();
    if (expected.has_value() != actual.has_value()) {
      std::cerr << "Ray " << index << " origin " << origin.x << ',' << origin.y
                << ',' << origin.z << " direction " << direction.x << ','
                << direction.y << ',' << direction.z << " expected "
                << expected.has_value() << " actual " << actual.has_value()
                << '\n';
      std::abort();
    }
    if (actual) {
      assert(std::abs(actual->x - expected->x) < .002F);
      assert(std::abs(actual->y - expected->y) < .002F);
      assert(std::abs(actual->z - expected->z) < .002F);
      const auto sampled = sampleTerrainHeight(*field, {actual->x, actual->z});
      assert(sampled && std::abs(*sampled - actual->y) < .002F);
    }
  }
  assert(!raycastTerrain(*field, {0, 50, 0}, {0, 0, 0}));
  assert(!raycastTerrain(*field, {0, 50, 0}, {0, 1, 0}));
  assert(raycastTerrain(*field, {0, -50, 0}, {0, 1, 0}));
  assert(!sampleTerrainHeight(*field, {1000, 0}));
  std::cout << "250 terrain rays: exhaustive_ms=" << exhaustiveMs
            << " grid_ms=" << gridMs << '\n';
}
