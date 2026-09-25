#pragma once
#include "demi/runtime/scene/components/3dcomponents/FractureDebris3DComponent.h"
#include <map>
#include <string>
#include <vector>

namespace demi::runtime {
struct World;
struct CosmeticFragment3D {
  Vec3 position, rotation, size, velocity, angularVelocity, gravity;
  Color color;
  std::string model, texture, renderLayer;
  float age = 0, lifetime = 0, fadeDuration = 0;
  float opacity() const;
};
// Cosmetic-only storage: no entities, collision shapes or physics handles.
class CosmeticDebris3D {
public:
  void emit(const std::string &owner, std::uint64_t attachment,
            const FractureDebris3DComponent &, Vec3 position, Vec3 direction);
  void update(const World &, float dt);
  void clear(const std::string &owner) { groups_.erase(owner); }
  std::vector<CosmeticFragment3D> snapshot() const;

private:
  struct Group {
    std::uint64_t attachment = 0;
    std::uint32_t random = 1;
    std::vector<CosmeticFragment3D> fragments;
  };
  std::map<std::string, Group> groups_;
};
} // namespace demi::runtime
