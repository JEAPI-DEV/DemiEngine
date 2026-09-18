#include "demi/runtime/destruction/DestructionImpact3D.h"
#include <algorithm>
#include <cmath>
namespace demi::runtime {
bool validateDestructionImpact3D(const DestructionImpact3D &hit,
                                 std::string &error) {
  const auto vector = [](Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::abs(v.x) <= 1e6F && std::abs(v.y) <= 1e6F &&
           std::abs(v.z) <= 1e6F;
  };
  if (!vector(hit.position) || !vector(hit.direction) ||
      !std::isfinite(hit.radius) || hit.radius < 0.001F || hit.radius > 1000 ||
      !std::isfinite(hit.energy) || hit.energy < 0 || hit.energy > 1e12F ||
      !std::isfinite(hit.impulse) || hit.impulse < 0 || hit.impulse > 1e9F ||
      (hit.energy == 0 && hit.impulse == 0)) {
    error = "Impact requires finite position/direction (magnitude per axis <= "
            "1e6), radius 0.001..1000, energy 0..1e12 J and impulse 0..1e9 "
            "N*s; at least one budget must be positive";
    return false;
  }
  error.clear();
  return true;
}
float destructionImpactWeight(float distance, float radius) {
  return std::clamp(1.0F - distance / radius, 0.0F, 1.0F);
}
Vec3 destructionImpactDirection(const DestructionImpact3D &hit, Vec3 contact) {
  Vec3 v = hit.direction;
  if (v.x == 0 && v.y == 0 && v.z == 0)
    v = {contact.x - hit.position.x, contact.y - hit.position.y,
         contact.z - hit.position.z};
  const double length =
      std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
  return length > 1e-9 ? Vec3{float(v.x / length), float(v.y / length),
                              float(v.z / length)}
                       : Vec3{0, 1, 0};
}
} // namespace demi::runtime
