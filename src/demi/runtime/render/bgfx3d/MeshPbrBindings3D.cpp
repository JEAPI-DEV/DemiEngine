#include "demi/runtime/render/bgfx3d/MeshPbrBindings3D.h"
#include "demi/runtime/render/bgfx3d/TextureProjectionPhase3D.h"

#include <cmath>
#include <utility>

namespace demi::runtime::render {
namespace {
using Slot = assets::MaterialMapSlot;
constexpr std::array slots{Slot::Normal,       Slot::Roughness,
                           Slot::Metallic,     Slot::AmbientOcclusion,
                           Slot::DetailNormal, Slot::Emissive};
constexpr std::array samplerNames{"s_pbrNormal",       "s_pbrRoughness",
                                  "s_pbrMetallic",     "s_pbrOcclusion",
                                  "s_pbrDetailNormal", "s_pbrEmissive"};
constexpr std::array uniformNames{"u_pbrMapFlags",         "u_pbrMapFlags2",
                                  "u_pbrSurface",          "u_pbrLocalScale",
                                  "u_pbrProjectionOrigin", "u_pbrFoliage"};
} // namespace

bool usesMeshPbrSurface3D(const MaterialBinding *material,
                          const MeshRendererComponent &mesh) {
  if (material && material->program) {
    return false;
  }
  for (const auto &[name, neutral] :
       {std::pair{"foliage_transmission", 0.F},
        std::pair{"ambient_occlusion_strength", 1.F}}) {
    const auto value = mesh.materialNumbers.find(name);
    if ((value != mesh.materialNumbers.end() && value->second != neutral) ||
        mesh.materialColors.contains(name)) {
      return true;
    }
  }
  if (!material) {
    return false;
  }
  const auto tiling = mesh.materialNumbers.find("tiling");
  // Invalid overrides also select this path so preparation diagnoses them.
  const bool overridesProjection =
      tiling != mesh.materialNumbers.end() && tiling->second != 1.F;
  return material->usesPbrSurface || overridesProjection;
}

MeshPbrBindings3D::MeshPbrBindings3D(GpuResources &resources,
                                     RenderCommands &commands,
                                     TextureResolver resolve)
    : resources_(resources), commands_(commands), resolve_(std::move(resolve)) {
}

MeshPbrBindings3D::~MeshPbrBindings3D() { shutdown(); }

void MeshPbrBindings3D::shutdown() {
  reset();
  for (const auto handle : samplers_) {
    if (handle) {
      resources_.destroy(handle);
    }
  }
  for (const auto handle : uniforms_) {
    if (handle) {
      resources_.destroy(handle);
    }
  }
  if (windUniform_) {
    resources_.destroy(windUniform_);
    windUniform_ = {};
  }
  samplers_ = {};
  uniforms_ = {};
}

bool MeshPbrBindings3D::initialize(std::string &error) {
  for (std::size_t index = 0; index < samplers_.size(); ++index) {
    if (!samplers_[index]) {
      samplers_[index] = resources_.createSampler(samplerNames[index], error);
      if (!samplers_[index]) {
        return false;
      }
    }
  }
  for (std::size_t index = 0; index < uniforms_.size(); ++index) {
    if (!uniforms_[index]) {
      uniforms_[index] = resources_.createUniform(uniformNames[index],
                                                  UniformType::Vec4, 1, error);
      if (!uniforms_[index]) {
        return false;
      }
    }
  }
  return true;
}

void MeshPbrBindings3D::reset() {
  enabled_ = false;
  windValues_ = {};
}

bool MeshPbrBindings3D::prepare(const MaterialBinding *material,
                                const MeshRendererComponent &mesh,
                                TextureHandle white, float terrainTextureScale,
                                std::string &error) {
  reset();
  if (material && !material->program) {
    const auto &surface = material->surface;
    windValues_ = {surface.windStrength, surface.windFrequency,
                   surface.windHeight, frameTime_};
  }
  if (!usesMeshPbrSurface3D(material, mesh)) {
    return true;
  }
  if (resources_.limits().maxTextureSamplers < 9) {
    error = "PBR material requires nine texture samplers on this device; "
            "maps cannot be silently replaced by scalar shading";
    return false;
  }
  if (!initialize(error)) {
    return false;
  }
  const assets::MaterialSurface neutralSurface;
  const auto &surface = material ? material->surface : neutralSurface;
  const auto scalarOverride = [&](std::string_view name, float fallback) {
    const auto value = mesh.materialNumbers.find(std::string(name));
    return value == mesh.materialNumbers.end() ? fallback : value->second;
  };
  const float transmission =
      scalarOverride("foliage_transmission", surface.foliageTransmission);
  const float occlusionStrength = scalarOverride(
      "ambient_occlusion_strength", surface.ambientOcclusionStrength);
  if (mesh.materialColors.contains("foliage_transmission") ||
      mesh.materialColors.contains("ambient_occlusion_strength") ||
      !std::isfinite(transmission) || transmission < 0 || transmission > 1 ||
      !std::isfinite(occlusionStrength) || occlusionStrength < 0 ||
      occlusionStrength > 1) {
    error = "Foliage transmission and ambient occlusion strength must be "
            "numbers in [0, 1]";
    return false;
  }
  const float materialTiling = scalarOverride("tiling", surface.tiling);
  const float normalStrength =
      scalarOverride("normal_strength", surface.normalStrength);
  const float detailStrength =
      scalarOverride("detail_strength", surface.detailStrength);
  if (!std::isfinite(normalStrength) || normalStrength < 0 ||
      !std::isfinite(detailStrength) || detailStrength < 0) {
    error = "Material normal/detail strength overrides must be finite and "
            "nonnegative";
    return false;
  }
  const float tiling = materialTiling * terrainTextureScale;
  if (!std::isfinite(tiling) || !(tiling > 0)) {
    error = "Material and terrain texture scale product must be finite and "
            "positive";
    return false;
  }
  values_ = {};
  values_[5] = {transmission, occlusionStrength, 0, 0};
  values_[1][2] =
      surface.mapping == assets::MaterialMapping::Triplanar ? 1.F : 0.F;
  values_[2] = {normalStrength, materialTiling, detailStrength,
                surface.normalMapYDown ? -1.F : 1.F};
  values_[3] = {mesh.size.x, mesh.size.y, mesh.size.z, terrainTextureScale};
  const auto projectionScale =
      surface.mapping == assets::MaterialMapping::Triplanar ? mesh.size
                                                            : Vec3{1, 1, 1};
  try {
    values_[4] = {textureProjectionPhase3D(
                      scalarOverride("projection_origin_x", 0),
                      double(tiling) * projectionScale.x,
                      (material ? material->terrainWrap : TextureWrap::Repeat)),
                  textureProjectionPhase3D(
                      scalarOverride("projection_origin_y", 0),
                      double(tiling) * projectionScale.y,
                      (material ? material->terrainWrap : TextureWrap::Repeat)),
                  textureProjectionPhase3D(
                      scalarOverride("projection_origin_z", 0),
                      double(tiling) * projectionScale.z,
                      (material ? material->terrainWrap : TextureWrap::Repeat)),
                  0};
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  for (std::size_t index = 0; index < slots.size(); ++index) {
    TextureHandle texture = white;
    const auto map = surface.maps.find(slots[index]);
    if (map != surface.maps.end()) {
      const auto semantic =
          slots[index] == Slot::Emissive ? ImageMipSemantic::Color
          : (slots[index] == Slot::Normal || slots[index] == Slot::DetailNormal)
              ? ImageMipSemantic::Normal
              : ImageMipSemantic::Linear;
      const auto view = resolve_(map->second, semantic);
      texture = view.handle;
      if (slots[index] == Slot::Emissive && !view.srgbDecoded) {
        values_[1][3] = 1;
      }
      if (!texture) {
        error = "PBR map is not loaded with its required sampling semantic: " +
                map->second;
        return false;
      }
      if (index < 4) {
        values_[0][index] = 1;
      } else {
        values_[1][index - 4] = 1;
      }
    }
    bindings_[index] = {.stage = std::uint8_t(index + 3),
                        .texture = texture,
                        .sampler = samplers_[index]};
  }
  // Explicit mesh properties remain overrides, rather than texture factors.
  if (mesh.roughness || mesh.materialNumbers.contains("roughness")) {
    values_[0][1] = 0;
  }
  if (mesh.metallic || mesh.materialNumbers.contains("metallic")) {
    values_[0][2] = 0;
  }
  enabled_ = true;
  return true;
}

std::uint32_t
MeshPbrBindings3D::availableInstances(std::uint16_t stride,
                                      std::uint32_t requested) const {
  return commands_.availableInstances(stride, requested);
}
bool MeshPbrBindings3D::configureView2D(const View2DConfig &view,
                                        std::string &error) {
  return commands_.configureView2D(view, error);
}
bool MeshPbrBindings3D::configureView3D(const View3DConfig &view,
                                        std::string &error) {
  return commands_.configureView3D(view, error);
}
bool MeshPbrBindings3D::submit(const TransientDraw &draw, std::string &error) {
  return commands_.submit(draw, error);
}

template <class Draw>
bool MeshPbrBindings3D::submitMesh(Draw draw, std::string &error) {
  if (!windUniform_) {
    windUniform_ =
        resources_.createUniform("u_meshWind", UniformType::Vec4, 1, error);
    if (!windUniform_) {
      return false;
    }
  }
  joinedUniforms_.assign(draw.uniforms.begin(), draw.uniforms.end());
  if (enabled_) {
    for (std::size_t index = 0; index < uniforms_.size(); ++index) {
      joinedUniforms_.push_back(
          {.handle = uniforms_[index], .values = values_[index]});
    }
    joinedTextures_.assign(draw.textures.begin(), draw.textures.end());
    joinedTextures_.insert(joinedTextures_.end(), bindings_.begin(),
                           bindings_.end());
    draw.textures = joinedTextures_;
  }
  // Append the independent wind binding after the established PBR layout.
  // Disabled surfaces still publish zero to prevent previous-draw leakage.
  joinedUniforms_.push_back({.handle = windUniform_, .values = windValues_});
  draw.uniforms = joinedUniforms_;
  return commands_.submit(draw, error);
}
bool MeshPbrBindings3D::submit(const BufferedDraw &draw, std::string &error) {
  return submitMesh(draw, error);
}
bool MeshPbrBindings3D::submit(const InstancedBufferedDraw &draw,
                               std::string &error) {
  return submitMesh(draw, error);
}
} // namespace demi::runtime::render
