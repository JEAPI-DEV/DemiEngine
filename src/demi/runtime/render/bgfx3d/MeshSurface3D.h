#pragma once

#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <array>
#include <optional>
#include <string_view>

namespace demi::runtime::render {

// The resolved surface is a per-draw value. Keeping it out of GPU resource
// caches makes live material and entity edits visible on the next frame.
struct MeshSurface3D {
  float metallic = 0.0F;
  float roughness = assets::MaterialSurface::DefaultRoughness;
  float opacity = assets::MaterialSurface::DefaultOpacity;
  std::array<float, 4> baseColor{1.0F, 1.0F, 1.0F, 1.0F};
  BlendMode blend = BlendMode::Opaque;
  DrawState state{.blend = BlendMode::Opaque,
                  .depthTest = DepthTest::Less,
                  .cull = CullMode::None,
                  .topology = PrimitiveTopology::Triangles,
                  .writeDepth = true};

  [[nodiscard]] bool transparent() const { return blend != BlendMode::Opaque; }
  [[nodiscard]] std::array<float, 4> shaderParameters() const {
    return {metallic, roughness, 0.0F, 0.0F};
  }
};

[[nodiscard]] inline MeshSurface3D
resolveMeshSurface3D(const MeshRendererComponent &mesh,
                     const MaterialBinding *material,
                     std::optional<float> fade = std::nullopt) {
  MeshSurface3D surface;
  if (material) {
    surface.state = material->state;
    surface.blend = material->state.blend;
    surface.metallic = material->metallic;
    surface.roughness = material->roughness;
    surface.opacity = material->opacity;
    surface.baseColor = material->baseColor;
  }
  if (const auto found = mesh.materialColors.find("base_color");
      found != mesh.materialColors.end())
    surface.baseColor = {found->second.r, found->second.g, found->second.b,
                         found->second.a};
  surface.metallic = assets::resolveMaterialSurfaceNumber(
      "metallic", &mesh.materialNumbers, mesh.metallic, surface.metallic);
  surface.roughness = assets::resolveMaterialSurfaceNumber(
      "roughness", &mesh.materialNumbers, mesh.roughness, surface.roughness);
  surface.opacity = assets::resolveMaterialSurfaceNumber(
      "opacity", &mesh.materialNumbers, mesh.opacity, surface.opacity);
  if (mesh.surfaceMode) {
    const std::string_view mode = *mesh.surfaceMode;
    surface.blend = mode == "additive" ? BlendMode::Additive
                  : mode == "transparent" ? BlendMode::Alpha
                  : BlendMode::Opaque;
  }
  if (fade)
    surface.blend = BlendMode::Alpha;
  surface.state.blend = surface.blend;
  if (surface.transparent())
    surface.state.writeDepth = false;
  return surface;
}

} // namespace demi::runtime::render
