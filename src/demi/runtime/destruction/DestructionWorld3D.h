#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace demi::runtime {
struct World;
class PhysicsWorld3D;
struct DestructionState3D {
  std::string root;
  std::string status;
  std::string error;
  std::uint64_t revision = 0;
  std::size_t bodies = 0;
  std::map<std::string, std::string> parts;
};
class DestructionWorld3D {
public:
  DestructionWorld3D();
  ~DestructionWorld3D();
  DestructionWorld3D(const DestructionWorld3D &) = delete;
  DestructionWorld3D &operator=(const DestructionWorld3D &) = delete;
  void prune(World &world);
  // Called only after native synchronization and before simulation.
  bool update(World &world, PhysicsWorld3D &physics);
  bool damagePart(const std::string &entity, const std::string &part,
                  float amount, std::string &error);
  DestructionState3D state(const std::string &entity) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::runtime
