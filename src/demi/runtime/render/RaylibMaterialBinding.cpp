#include "demi/runtime/render/RaylibMaterialBinding.h"

namespace demi::runtime {

void applyMaterialShaderParameters(const Shader &shader,
                                   const assets::MaterialAsset &material) {
  for (const auto &[name, value] : material.numbers) {
    const int location = GetShaderLocation(shader, name.c_str());
    if (location >= 0)
      SetShaderValue(shader, location, &value, SHADER_UNIFORM_FLOAT);
  }
  for (const auto &[name, value] : material.colors) {
    const float channels[]{value.r, value.g, value.b, value.a};
    const int location = GetShaderLocation(shader, name.c_str());
    if (location >= 0)
      SetShaderValue(shader, location, channels, SHADER_UNIFORM_VEC4);
  }
}

ScopedRaylibMaterial2D::ScopedRaylibMaterial2D(
    const ShaderResourceLibrary &shaders,
    const assets::MaterialAsset *material) {
  if (material == nullptr)
    return;
  if (material->renderState.blend == "additive") {
    BeginBlendMode(BLEND_ADDITIVE);
    additiveBlendActive_ = true;
  }
  if (const Shader *shader = shaders.find(material->shader)) {
    applyMaterialShaderParameters(*shader, *material);
    BeginShaderMode(*shader);
    shaderActive_ = true;
  }
}

ScopedRaylibMaterial2D::~ScopedRaylibMaterial2D() {
  if (shaderActive_)
    EndShaderMode();
  if (additiveBlendActive_)
    EndBlendMode();
}

} // namespace demi::runtime
