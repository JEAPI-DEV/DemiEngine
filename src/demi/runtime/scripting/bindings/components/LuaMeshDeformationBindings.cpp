#include "demi/runtime/scripting/bindings/components/LuaMeshDeformationBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

namespace demi::runtime {
void LuaMeshDeformationBindingModule::install(LuaScriptHost &host,
                                              lua_State *state) const {
  sol::table mesh =
      sol::state_view(state).create_named_table("MeshDeformation");
  mesh.set_function("impact", [&host](const std::string &id, sol::table contact,
                                      sol::optional<sol::table> options) {
    MeshImpactMaterial3D material = host.meshImpactMaterial(id);
    if (options) {
      material.radius = options->get_or("radius", material.radius);
      material.yieldEnergy =
          options->get_or("yield_energy", material.yieldEnergy);
      material.stiffness = options->get_or("stiffness", material.stiffness);
      material.absorption = options->get_or("absorption", material.absorption);
      material.maximumDepth =
          options->get_or("max_depth", material.maximumDepth);
    }
    const auto result = host.impactMesh(
        id,
        {contact.get<float>("point_x"), contact.get<float>("point_y"),
         contact.get<float>("point_z")},
        {contact.get<float>("normal_x"), contact.get<float>("normal_y"),
         contact.get<float>("normal_z")},
        contact.get<float>("impact_energy"), material);
    return std::make_tuple(result.accepted, result.error, result.depth);
  });
  mesh.set_function("dent", [&host](const std::string &id, sol::table impact) {
    const std::string error = host.dentMesh(
        id, luaVec3Field(impact, "point"), luaVec3Field(impact, "direction"),
        impact.get_or("radius", 0.0F), impact.get_or("depth", 0.0F));
    return std::make_tuple(error.empty(), error);
  });
  mesh.set_function("reset", [&host](const std::string &id) {
    return host.resetMeshDents(id);
  });
}
} // namespace demi::runtime
