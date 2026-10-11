#pragma once

#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"

#include <array>
#include <vector>

namespace demi::runtime::render {

// Owns all per-surface uniform backing values through synchronous submission.
// Fill descriptors after construction so returning/moving this value cannot
// leave spans pointing into an earlier address.
struct PreparedMeshDraw3D {
  PreparedMeshDraw3D(const MeshRendererComponent &mesh,
                     const MaterialBinding *material, std::optional<float> fade,
                     bool unlit, bool shadowPass, bool overdraw,
                     ProgramHandle defaultProgram);

  void fillUniforms(const std::array<DrawUniformValue, 8> &lighting,
                    const MaterialBinding *material,
                    std::vector<DrawUniformValue> &scratch) const;

  MeshSurface3D surface;
  DrawState state;
  ProgramHandle program;
  std::array<float, 4> surfaceTint;
  std::array<float, 4> builtinSurfaceTint;
  std::array<float, 4> metalRough;
  std::array<float, 4> alphaCutoff;
  bool unlit = false;

private:
  const std::array<float, 4> unlitAmbient_{1, 1, 1, 1};
  const std::array<float, 4> noLight_{};
};

} // namespace demi::runtime::render
