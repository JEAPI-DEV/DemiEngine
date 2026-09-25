#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "demi/runtime/destruction/DestructionImpact3D.h"
#include "demi/runtime/destruction/CosmeticDebris3D.h"

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
  void updateCosmetics(const World &world, float dt) { cosmetics_.update(world, dt); }
  std::vector<CosmeticFragment3D> cosmeticFragments() const { return cosmetics_.snapshot(); }
  // Called only after native synchronization and before simulation.
  bool update(World &world, PhysicsWorld3D &physics);
  bool damagePart(const std::string &entity, const std::string &part,
                  float amount, std::string &error);
  bool impact(World &world, PhysicsWorld3D &physics, const DestructionImpact3D &impact,
              std::size_t &affectedAssemblies, std::string &error);
  DestructionState3D state(const std::string &entity) const;
  nlohmann::json checkpoint(const World &world, const std::string &entity,
                            std::string &error) const;
  bool restore(const std::string &entity, const nlohmann::json &checkpoint,
               std::string &error);
  bool retireDebris(const std::string &entity, std::string &error);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  CosmeticDebris3D cosmetics_;
};
} // namespace demi::runtime
