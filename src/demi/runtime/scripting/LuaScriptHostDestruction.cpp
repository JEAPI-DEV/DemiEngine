#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
namespace demi::runtime {
bool LuaScriptHost::applyDestructionImpact3D(const DestructionImpact3D &impact,
                                             std::size_t &affectedAssemblies,
                                             std::string &error) {
  affectedAssemblies = 0;
  if (!world_ || !world_->destruction3D) {
    error = "Destruction attaches after the first physics step";
    return false;
  }
  return world_->destruction3D->impact(*world_, ensurePhysicsWorld3D(*world_),
                                       impact, affectedAssemblies, error);
}
bool LuaScriptHost::damageDestructiblePart3D(const std::string &entity,
                                             const std::string &part,
                                             float damage, std::string &error) {
  if (!world_ || !world_->destruction3D) {
    error = "Destruction attaches after the first physics step";
    return false;
  }
  return world_->destruction3D->damagePart(entity, part, damage, error);
}
DestructionState3D
LuaScriptHost::destructionState3D(const std::string &entity) const {
  return world_ && world_->destruction3D
             ? world_->destruction3D->state(entity)
             : DestructionState3D{.root = {},
                                  .status = "unattached",
                                  .error = {},
                                  .parts = {}};
}
} // namespace demi::runtime
