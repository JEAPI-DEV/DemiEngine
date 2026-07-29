#pragma once

#include "demi/assets/RenderAsset.h"
#include "demi/runtime/render/ShaderResourceLibrary.h"

namespace demi::runtime {

void applyMaterialShaderParameters(const Shader &shader,
                                   const assets::MaterialAsset &material);

// Applies the shader and 2D-compatible render state for one draw scope.
class ScopedRaylibMaterial2D {
public:
  ScopedRaylibMaterial2D(const ShaderResourceLibrary &shaders,
                         const assets::MaterialAsset *material);
  ~ScopedRaylibMaterial2D();

  ScopedRaylibMaterial2D(const ScopedRaylibMaterial2D &) = delete;
  ScopedRaylibMaterial2D &
  operator=(const ScopedRaylibMaterial2D &) = delete;

private:
  bool shaderActive_ = false;
  bool additiveBlendActive_ = false;
};

} // namespace demi::runtime
