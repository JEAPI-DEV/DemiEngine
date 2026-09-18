#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace demi::assets {
using FracturePoint = std::array<double, 3>;
struct FractureVertex {
  FracturePoint position;
  std::array<double, 2> uv{};
};
struct FractureFace {
  std::vector<FractureVertex> vertices;
  bool interior = false;
};
struct FractureSolid {
  std::string id;
  std::vector<FractureFace> faces;
};
// Closed convex source only. Throws a diagnostic on unsupported/non-manifold
// geometry; never replaces an arbitrary source with a bounding box.
void validateFractureSolid(FractureSolid &solid);
std::vector<FractureSolid> fractureConvexSolid(FractureSolid source,
                                               std::size_t pieces,
                                               std::uint32_t seed);
std::vector<FracturePoint> fracturePoints(const FractureSolid &solid);
double fractureVolume(const FractureSolid &solid);
FracturePoint fractureNormal(const FractureFace &face);
bool fractureSolidsTouch(const FractureSolid &first,
                         const FractureSolid &second);
} // namespace demi::assets
