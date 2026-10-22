#include "demi/runtime/render/BgfxRenderer3D.h"

namespace demi::runtime::render {

bool BgfxRenderer3D::prepareAnalyticDepthPrograms(std::string &error) {
  // Only programs available to the ordinary renderer need an output variant.
  // Material capability diagnostics remain owned by the ordinary path.
  const std::pair<ProgramHandle, BuiltinProgram> programs[]{
      {meshProgram_, BuiltinProgram::Lit3D},
      {instancedMeshProgram_, BuiltinProgram::Lit3DInstanced},
      {skinnedMeshProgram_, BuiltinProgram::Lit3DSkinned},
      {directionalMeshProgram_, BuiltinProgram::Directional3D},
      {directionalInstancedProgram_, BuiltinProgram::Directional3DInstanced},
      {directionalSkinnedProgram_, BuiltinProgram::Directional3DSkinned},
      {pbrPrograms_[0], BuiltinProgram::Pbr3D},
      {pbrPrograms_[1], BuiltinProgram::Pbr3DInstanced},
      {pbrPrograms_[2], BuiltinProgram::Pbr3DSkinned},
      {pbrPrograms_[3], BuiltinProgram::Pbr3DDirectional},
      {pbrPrograms_[4], BuiltinProgram::Pbr3DDirectionalInstanced},
      {pbrPrograms_[5], BuiltinProgram::Pbr3DDirectionalSkinned},
      {terrainProgram_, BuiltinProgram::Terrain3D},
      {directionalTerrainProgram_, BuiltinProgram::Terrain3DDirectional},
      {mappedTerrainProgram_, BuiltinProgram::TerrainMapped3D},
      {directionalMappedTerrainProgram_,
       BuiltinProgram::TerrainMapped3DDirectional},
      {opaqueMappedTerrainProgram_, BuiltinProgram::TerrainMappedOpaque3D},
      {directionalOpaqueMappedTerrainProgram_,
       BuiltinProgram::TerrainMappedOpaque3DDirectional}};
  for (const auto &[ordinary, builtin] : programs) {
    if (!ordinary || analyticDepthProgram(ordinary, true)) {
      continue;
    }
    const auto program = resources_.createBuiltinProgram(builtin, true, error);
    if (!program) {
      return false;
    }
    analyticDepthPrograms_.emplace_back(ordinary, program);
  }
  return true;
}

ProgramHandle BgfxRenderer3D::analyticDepthProgram(ProgramHandle program,
                                                   bool enabled) const {
  if (!enabled || !program) {
    return program;
  }
  for (const auto &[ordinary, analytic] : analyticDepthPrograms_) {
    if (ordinary == program) {
      return analytic;
    }
  }
  return {};
}

} // namespace demi::runtime::render
