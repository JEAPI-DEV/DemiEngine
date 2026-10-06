#pragma once

#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace demi::runtime::render {

// The resolved surface is a per-draw value. Keeping it out of GPU resource
// caches makes live material and entity edits visible on the next frame.
struct MeshSurface3D {
  float metallic = 0.0F;
  float roughness = 0.8F;
  float opacity = 1.0F;
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

// Exact float bits keep different opaque surfaces out of the same instance
// group even when their decimal display values would round identically.
[[nodiscard]] inline std::string surfaceBatchKey3D(const MeshSurface3D &surface) {
  return std::to_string(std::bit_cast<std::uint32_t>(surface.metallic)) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.roughness)) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.opacity)) + ":" +
         std::to_string(static_cast<int>(surface.blend)) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.baseColor[0])) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.baseColor[1])) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.baseColor[2])) + ":" +
         std::to_string(std::bit_cast<std::uint32_t>(surface.baseColor[3]));
}

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
    surface.baseColor = {found->second.r, found->second.g,
                         found->second.b, found->second.a};
  if (const auto found = mesh.materialNumbers.find("metallic");
      found != mesh.materialNumbers.end())
    surface.metallic = found->second;
  if (const auto found = mesh.materialNumbers.find("roughness");
      found != mesh.materialNumbers.end())
    surface.roughness = found->second;
  if (const auto found = mesh.materialNumbers.find("opacity");
      found != mesh.materialNumbers.end())
    surface.opacity = found->second;
  if (mesh.metallic)
    surface.metallic = *mesh.metallic;
  if (mesh.roughness)
    surface.roughness = *mesh.roughness;
  if (mesh.opacity)
    surface.opacity = *mesh.opacity;
  if (mesh.surfaceMode) {
    const std::string_view mode = *mesh.surfaceMode;
    surface.blend = mode == "additive" ? BlendMode::Additive
                  : mode == "transparent" ? BlendMode::Alpha
                  : BlendMode::Opaque;
  }
  if (fade)
    surface.blend = BlendMode::Alpha;
  surface.metallic = std::isfinite(surface.metallic)
                         ? std::clamp(surface.metallic, 0.0F, 1.0F) : 0.0F;
  surface.roughness = std::isfinite(surface.roughness)
                          ? std::clamp(surface.roughness, 0.0F, 1.0F) : 0.8F;
  surface.opacity = std::isfinite(surface.opacity)
                        ? std::clamp(surface.opacity, 0.0F, 1.0F) : 1.0F;
  surface.state.blend = surface.blend;
  if (surface.transparent())
    surface.state.writeDepth = false;
  return surface;
}

} // namespace demi::runtime::render
