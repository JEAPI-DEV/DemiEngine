#include "demi/runtime/render/bgfx3d/TerrainGpuFieldData3D.h"

#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialResidency3D.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace demi::runtime::render {
namespace {

class UnsupportedField final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

DrawState builtinState() {
  return {.blend = BlendMode::Opaque,
          .depthTest = DepthTest::Less,
          .cull = CullMode::None,
          .topology = PrimitiveTopology::Triangles,
          .writeDepth = true};
}

bool samePipelineExceptCull(const DrawState &first, const DrawState &second) {
  return first.blend == second.blend && first.depthTest == second.depthTest &&
         first.topology == second.topology &&
         first.writeColor == second.writeColor &&
         first.writeAlpha == second.writeAlpha &&
         first.writeDepth == second.writeDepth &&
         first.multisampling == second.multisampling;
}

void requireCompatibleState(const DrawState &state) {
  if (state.blend != BlendMode::Opaque || state.depthTest != DepthTest::Less ||
      (state.cull != CullMode::None && state.cull != CullMode::Clockwise) ||
      state.topology != PrimitiveTopology::Triangles || !state.writeColor ||
      !state.writeAlpha || !state.writeDepth || !state.multisampling) {
    throw UnsupportedField(
        "Terrain material requires unsupported raster/depth state");
  }
}

void requireIntrinsicParameters(const MaterialBinding &material) {
  for (const auto &parameter : material.parameters) {
    const bool intrinsic =
        parameter.name == "base_color" || parameter.name == "metallic" ||
        parameter.name == "roughness" || parameter.name == "opacity";
    if (!intrinsic) {
      throw UnsupportedField(
          "Terrain material has a shader-specific parameter: " +
          parameter.name);
    }
  }
  for (const auto &uniform : material.uniforms) {
    const auto known =
        std::find_if(material.parameters.begin(), material.parameters.end(),
                     [&](const auto &parameter) {
                       return parameter.uniform == uniform.handle;
                     });
    if (known == material.parameters.end()) {
      throw UnsupportedField(
          "Terrain material has an unclassified uniform override");
    }
  }
}

struct ResolvedMaterial {
  DrawState state = builtinState();
  std::array<float, 4> baseColor{1, 1, 1, 1};
  float metallic = 0;
  float roughness = 0.8F;
  bool unlit = false;
  assets::MaterialSurface surface;
  TextureWrap wrap = TextureWrap::Repeat;
};

ResolvedMaterial resolve(const std::string &id,
                         const TerrainGpuMaterialResolver3D &resolver) {
  if (id.empty()) {
    return {};
  }
  const auto *material = resolver ? resolver(id) : nullptr;
  if (!material) {
    throw UnsupportedField("Terrain material is unavailable: " + id);
  }
  if (material->program) {
    throw UnsupportedField("Terrain material uses a custom shader: " + id);
  }
  if (!material->albedoTexture.empty() &&
      !material->surface.maps.contains(assets::MaterialMapSlot::Albedo)) {
    throw UnsupportedField(
        "Terrain albedo binding is missing its shared surface map: " + id);
  }
  if (material->alphaCutoff != 0 || material->opacity != 1) {
    throw UnsupportedField(
        "Terrain material requires cutout or transparency: " + id);
  }
  requireCompatibleState(material->state);
  requireIntrinsicParameters(*material);
  if (!std::isfinite(material->metallic) ||
      !std::isfinite(material->roughness)) {
    throw UnsupportedField(
        "Terrain material contains a non-finite surface parameter: " + id);
  }
  for (const float channel : material->baseColor) {
    if (!std::isfinite(channel) || channel < 0) {
      throw UnsupportedField(
          "Terrain material contains an invalid base color: " + id);
    }
  }
  if (material->baseColor[3] != 1) {
    throw UnsupportedField(
        "Terrain material base color requires transparency: " + id);
  }
  return {.state = material->state,
          .baseColor = material->baseColor,
          .metallic = std::clamp(material->metallic, 0.0F, 1.0F),
          .roughness = std::clamp(material->roughness, 0.0F, 1.0F),
          .unlit = material->unlit,
          .surface = material->surface,
          .wrap = material->terrainWrap};
}

std::array<std::uint8_t, 4> encodeIndex(std::uint32_t index) {
  return {static_cast<std::uint8_t>(index),
          static_cast<std::uint8_t>(index >> 8U),
          static_cast<std::uint8_t>(index >> 16U),
          static_cast<std::uint8_t>(index >> 24U)};
}

class PaletteBuilder {
public:
  PaletteBuilder(const HeightField &field,
                 const TerrainGpuMaterialResolver3D &resolver,
                 std::uint64_t maximumEntries)
      : field_(field), resolver_(resolver), maximumEntries_(maximumEntries) {
    std::set<std::string> ids{""};
    ids.insert(field.biomeMaterials.begin(), field.biomeMaterials.end());
    if (field.surfaceMaterials) {
      for (const auto &[role, id] : field.surfaceMaterials->bindings) {
        ids.insert(id);
      }
    }
    materialIds_.assign(ids.begin(), ids.end());
  }

  std::uint32_t entry(std::size_t biome, std::size_t role) {
    const bool override = !field_.biomeMaterial(biome).empty();
    const auto key = std::pair{biome, override ? std::size_t{0} : role};
    if (const auto found = entries_.find(key); found != entries_.end()) {
      return found->second;
    }
    std::string id = field_.biomeMaterial(biome);
    if (!override && field_.surfaceMaterials) {
      const auto &classification = *field_.surfaceMaterials->classification;
      const auto binding =
          field_.surfaceMaterials->bindings.find(classification.roles.at(role));
      if (binding != field_.surfaceMaterials->bindings.end()) {
        id = binding->second;
      }
    }
    auto material = materials_.find(id);
    if (material == materials_.end()) {
      material = materials_.emplace(id, resolve(id, resolver_)).first;
    }
    const auto &resolved = material->second;
    if (!state_) {
      state_ = resolved.state;
    } else {
      if (!samePipelineExceptCull(*state_, resolved.state)) {
        throw UnsupportedField(
            "Terrain materials require mixed raster/depth states");
      }
      // Rasterize both sides for a mixed field. Each palette entry retains its
      // own CW-cull flag so the fragment program applies the material contract.
      if (state_->cull != resolved.state.cull) {
        state_->cull = CullMode::None;
      }
    }
    const auto color = field_.biomeColors.at(biome);
    const std::array<float, 4> tinted{
        color.r * resolved.baseColor[0], color.g * resolved.baseColor[1],
        color.b * resolved.baseColor[2], color.a * resolved.baseColor[3]};
    for (const float channel : tinted) {
      if (!std::isfinite(channel) || channel < 0) {
        throw UnsupportedField("Terrain biome contains an invalid tint");
      }
    }
    if (tinted[3] != 1) {
      throw UnsupportedField("Terrain biome tint requires transparency");
    }
    const std::array<float, 4> parameters{
        resolved.metallic, resolved.roughness, resolved.unlit ? 1.0F : 0.0F,
        resolved.state.cull == CullMode::Clockwise ? 1.0F : 0.0F};
    const auto &surface = resolved.surface;
    const bool mapped =
        !surface.maps.empty() || surface.macroVariationStrength > 0;
    mapped_ = mapped_ || mapped;
    const auto layer =
        std::lower_bound(materialIds_.begin(), materialIds_.end(), id);
    const float effectiveTiling =
        surface.tiling * field_.biomeTextureScale(biome);
    if (!std::isfinite(effectiveTiling) || effectiveTiling <= 0) {
      throw UnsupportedField(
          "Terrain material texture scale must remain finite and positive");
    }
    const std::array<float, 4> mapping{
        float(layer - materialIds_.begin()), effectiveTiling,
        surface.mapping == assets::MaterialMapping::Triplanar ? 1.F : 0.F,
        surface.normalMapYDown ? -1.F : 1.F};
    const std::array<float, 4> strengths{
        surface.maps.contains(assets::MaterialMapSlot::Normal)
            ? surface.normalStrength
            : 0.F,
        surface.maps.contains(assets::MaterialMapSlot::DetailNormal)
            ? surface.detailStrength
            : 0.F,
        surface.maps.contains(assets::MaterialMapSlot::Height)
            ? surface.heightBlendStrength
            : 0.F,
        surface.macroVariationStrength};
    const std::array<float, 4> detail{
        surface.macroVariationScale,
        float(terrainMaterialWrapCode3D(resolved.wrap)),
        surface.maps.contains(assets::MaterialMapSlot::Emissive) ? 1.F : 0.F, 0};
    std::array<std::uint32_t, 20> bits{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
      bits[channel] = std::bit_cast<std::uint32_t>(
          tinted[channel] == 0 ? 0.0F : tinted[channel]);
      bits[channel + 4] = std::bit_cast<std::uint32_t>(parameters[channel]);
    }
    for (std::size_t channel = 0; channel < 4; ++channel) {
      bits[channel + 8] = std::bit_cast<std::uint32_t>(mapping[channel]);
      bits[channel + 12] = std::bit_cast<std::uint32_t>(strengths[channel]);
      bits[channel + 16] = std::bit_cast<std::uint32_t>(detail[channel]);
    }
    if (!mapped) {
      // Mapping controls have no visible effect on a solid surface. Equal
      // scalar/tint entries can still share one palette record, even when
      // authored through different Material IDs.
      std::fill(bits.begin() + 8, bits.end(), 0U);
    }
    auto found = deduplicated_.find(bits);
    std::uint32_t index = 0;
    if (found != deduplicated_.end()) {
      index = found->second;
    } else {
      const auto count = texels_.size() / 5;
      if (count >= maximumEntries_ ||
          count >= std::numeric_limits<std::uint32_t>::max()) {
        throw UnsupportedField(
            "Terrain material palette exceeds device texture addressing");
      }
      index = static_cast<std::uint32_t>(count);
      deduplicated_.emplace(bits, index);
      texels_.push_back(tinted);
      texels_.push_back(parameters);
      texels_.push_back(mapping);
      texels_.push_back(strengths);
      texels_.push_back(detail);
    }
    entries_.emplace(key, index);
    return index;
  }

  const DrawState &state() const { return *state_; }
  void retainUsedMaterials() {
    std::set<std::string> selected{""};
    for (std::size_t offset = 0; offset < texels_.size(); offset += 5) {
      const auto layer = static_cast<std::size_t>(texels_[offset + 2][0]);
      selected.insert(materialIds_.at(layer));
    }
    std::vector<std::string> used(selected.begin(), selected.end());
    // The worker resolves only selected biome/role pairs. Unused palette
    // bindings must not load image sources or consume resident array layers.
    for (std::size_t offset = 0; offset < texels_.size(); offset += 5) {
      auto &layer = texels_[offset + 2][0];
      const auto &id = materialIds_.at(static_cast<std::size_t>(layer));
      layer =
          float(std::lower_bound(used.begin(), used.end(), id) - used.begin());
    }
    materialIds_ = std::move(used);
  }

  bool mapped() const { return mapped_; }
  const std::vector<std::string> &materialIds() const { return materialIds_; }
  std::vector<std::array<float, 4>> takeTexels() {
    if (mapped_) {
      return std::move(texels_);
    }
    std::vector<std::array<float, 4>> solid;
    solid.reserve(texels_.size() / 5 * 2);
    for (std::size_t index = 0; index < texels_.size(); index += 5) {
      solid.push_back(texels_[index]);
      solid.push_back(texels_[index + 1]);
    }
    return solid;
  }

private:
  const HeightField &field_;
  const TerrainGpuMaterialResolver3D &resolver_;
  std::uint64_t maximumEntries_;
  std::optional<DrawState> state_;
  std::map<std::string, ResolvedMaterial, std::less<>> materials_;
  std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> entries_;
  std::map<std::array<std::uint32_t, 20>, std::uint32_t> deduplicated_;
  std::vector<std::array<float, 4>> texels_;
  std::vector<std::string> materialIds_;
  bool mapped_ = false;
};

TerrainGpuFieldData3D prepare(const HeightField &field,
                              const TerrainGpuMaterialResolver3D &resolver,
                              std::uint32_t maximumTextureSize) {
  const auto limit = std::min<std::uint32_t>(
      maximumTextureSize, std::numeric_limits<std::uint16_t>::max());
  if (field.cellsX <= 0 || field.cellsZ <= 0 || limit == 0 ||
      !std::isfinite(field.size.x) || !std::isfinite(field.size.y) ||
      field.size.x <= 0 || field.size.y <= 0) {
    throw UnsupportedField("Terrain field or device texture extent is empty");
  }
  const auto width = std::uint64_t(field.cellsX) + 1;
  const auto height = std::uint64_t(field.cellsZ) + 1;
  if (width > limit || height > limit) {
    throw UnsupportedField("Terrain sample texture exceeds device dimensions");
  }
  const auto samples = std::size_t(width * height);
  if (field.heights.size() < samples || field.normals.size() < samples ||
      field.biomeIndices.size() < samples ||
      field.biomeColors.size() < field.biomeIds.size() ||
      (!field.biomeMaterials.empty() &&
       field.biomeMaterials.size() < field.biomeIds.size()) ||
      (!field.biomeTextureScales.empty() &&
       field.biomeTextureScales.size() < field.biomeIds.size())) {
    throw UnsupportedField(
        "Terrain field buffers or biome colors are incomplete");
  }
  if (field.surfaceMaterials &&
      (!field.surfaceMaterials->classification ||
       field.surfaceMaterials->classification->samples.size() < samples)) {
    throw UnsupportedField("Terrain surface classification is incomplete");
  }
  TerrainGpuFieldData3D result;
  result.sampleWidth = static_cast<std::uint16_t>(width);
  result.sampleHeight = static_cast<std::uint16_t>(height);
  result.heights.reserve(samples);
  result.normals.reserve(samples);
  result.materialIndices.reserve(samples);
  PaletteBuilder palette(field, resolver, std::uint64_t(limit) * limit / 2);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const auto biome = field.biomeIndices.at(sample);
    if (biome >= field.biomeIds.size()) {
      throw UnsupportedField("Terrain sample names an unknown biome");
    }
    std::size_t role = 0;
    if (field.surfaceMaterials && field.biomeMaterial(biome).empty()) {
      role = field.surfaceMaterials->classification->samples.at(sample);
      if (role >= field.surfaceMaterials->classification->roles.size()) {
        throw UnsupportedField("Terrain sample names an unknown material role");
      }
    }
    const float heightValue = field.heights.at(sample);
    const auto normal = field.normals.at(sample);
    if (!std::isfinite(heightValue) || !std::isfinite(normal.x) ||
        !std::isfinite(normal.y) || !std::isfinite(normal.z)) {
      throw UnsupportedField(
          "Terrain samples contain non-finite height or normals");
    }
    result.heights.push_back(heightValue);
    result.normals.push_back({normal.x, normal.y, normal.z, 0});
    result.materialIndices.push_back(encodeIndex(palette.entry(biome, role)));
  }
  result.state = palette.state();
  palette.retainUsedMaterials();
  result.mapped = palette.mapped();
  result.materialIds = palette.materialIds();
  result.palette = palette.takeTexels();
  result.paletteEntries = result.palette.size() / (result.mapped ? 5 : 2);
  if (result.palette.size() > std::uint64_t(limit) * limit) {
    throw UnsupportedField(
        "Terrain mapped palette exceeds device texture addressing");
  }
  result.paletteWidth = static_cast<std::uint16_t>(
      std::min<std::size_t>(limit, result.palette.size()));
  result.paletteHeight = static_cast<std::uint16_t>(
      (result.palette.size() + result.paletteWidth - 1) / result.paletteWidth);
  result.palette.resize(std::size_t(result.paletteWidth) *
                        result.paletteHeight);
  result.compatible = true;
  return result;
}

} // namespace

TerrainGpuFieldData3D prepareTerrainGpuFieldData3D(
    const HeightField &field,
    const TerrainGpuMaterialResolver3D &resolveMaterial,
    std::uint32_t maxTextureSize) {
  try {
    return prepare(field, resolveMaterial, maxTextureSize);
  } catch (const UnsupportedField &exception) {
    TerrainGpuFieldData3D result;
    result.fallbackReason = exception.what();
    return result;
  }
}

} // namespace demi::runtime::render
