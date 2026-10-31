#pragma once
#include <span>

#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"

#include <array>
#include <vector>

namespace demi::runtime::render {

// Pass-neutral, immutable surface values. Uniform spans are assembled only
// during submission, after this value has reached its final owning address.
struct MeshDrawSurface3D {
  MeshDrawSurface3D(const MeshRendererComponent &mesh,
                    const MaterialBinding *material, std::optional<float> fade,
                    bool unlit);
  void fillUniforms(std::span<const DrawUniformValue> lighting,
                    const MaterialBinding *material,
                    std::vector<DrawUniformValue> &scratch) const;

  MeshSurface3D surface;
  std::array<float, 4> surfaceTint;
  std::array<float, 4> builtinSurfaceTint;
  std::array<float, 4> metalRough;
  std::array<float, 4> alphaCutoff;
  bool unlit = false;

private:
  std::array<float, 4> unlitAmbient_{1, 1, 1, 1};
  std::array<float, 4> noLight_{};
};

// Adds only the current pass's program and raster-state decisions.
struct PreparedMeshDraw3D : MeshDrawSurface3D {
  PreparedMeshDraw3D(const MeshRendererComponent &mesh,
                     const MaterialBinding *material, std::optional<float> fade,
                     bool unlit, bool shadowPass, bool overdraw,
                     ProgramHandle defaultProgram);
  PreparedMeshDraw3D(const MeshDrawSurface3D &surface,
                     const MaterialBinding *material, bool depthOnly,
                     bool overdraw, ProgramHandle defaultProgram);

  DrawState state;
  ProgramHandle program;
};

} // namespace demi::runtime::render
