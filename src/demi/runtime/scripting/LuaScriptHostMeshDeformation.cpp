#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"

namespace demi::runtime {
MeshImpactMaterial3D LuaScriptHost::meshImpactMaterial(const std::string &id) const {
  const auto *entity = lookupServiceEntity(id);
  const auto *dentable = entity ? entity->component<Dentable3DComponent>() : nullptr;
  return dentable ? dentable->material : MeshImpactMaterial3D{};
}
std::string LuaScriptHost::dentMesh(const std::string &entityId, Vec3 point,
                                    Vec3 direction, float radius, float depth) {
  if (!world_)
    return "World is not loaded.";
  std::string error;
  (void)dentMesh3D(*world_, entityId, point, direction, radius, depth, error);
  return error;
}
bool LuaScriptHost::resetMeshDents(const std::string &entityId) {
  return world_ && resetMeshDents3D(*world_, entityId);
}
MeshImpactResult3D
LuaScriptHost::impactMesh(const std::string &id, Vec3 point, Vec3 direction,
                          float energy, const MeshImpactMaterial3D &material) {
  if (!world_)
    return {.error = "World is not loaded."};
  return impactMesh3D(*world_, id, point, direction, energy, material);
}
} // namespace demi::runtime
