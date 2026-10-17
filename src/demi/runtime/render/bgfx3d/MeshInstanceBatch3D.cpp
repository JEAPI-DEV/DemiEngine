#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"

#include <bit>
#include <functional>

namespace demi::runtime::render {

std::size_t MeshInstanceBatch3D::KeyHash::operator()(const Key &key) const {
  std::size_t hash = std::hash<const GpuMesh3D *>{}(key.mesh);
  const auto combine = [&hash](std::uint32_t value) {
    hash ^= std::hash<std::uint32_t>{}(value) + 0x9e3779b9U + (hash << 6U) +
            (hash >> 2U);
  };
  combine(key.texture.index);
  combine(key.texture.generation);
  for (const auto value : key.uniformBits) {
    combine(value);
  }
  combine(key.unlit ? 1U : 0U);
  return hash;
}

void MeshInstanceBatch3D::add(const GpuMesh3D &mesh, TextureHandle texture,
                              const std::array<float, 4> &tint,
                              const std::array<float, 4> &metalRough,
                              bool unlit,
                              const std::array<float, 16> &transform) {
  Key key{.mesh = &mesh, .texture = texture, .unlit = unlit};
  for (std::size_t index = 0; index < 4; ++index) {
    key.uniformBits[index] = std::bit_cast<std::uint32_t>(tint[index]);
    key.uniformBits[index + 4] =
        std::bit_cast<std::uint32_t>(metalRough[index]);
  }
  const auto [found, inserted] = groupLookup_.try_emplace(key, groups_.size());
  if (inserted) {
    groups_.push_back({.mesh = &mesh,
                       .texture = texture,
                       .tint = tint,
                       .metalRough = metalRough,
                       .unlit = unlit});
  }
  groups_[found->second].transforms.push_back(transform);
}

bool MeshInstanceBatch3D::flush(
    RenderCommands &commands, std::uint16_t view, ProgramHandle meshProgram,
    ProgramHandle instancedProgram, SamplerHandle sampler,
    const DrawState &state,
    const std::array<DrawUniformValue, 8> &lightingUniforms,
    std::uint32_t &draws, std::uint32_t &triangles, std::string &error) const {
  const std::array<float, 4> debugMode{lightingUniforms[2].values[0], 0, 0, 0};
  for (const auto &group : groups_) {
    auto groupUniforms = lightingUniforms;
    groupUniforms.front().values = group.tint;
    std::array<float, 4> groupMetalRough = group.metalRough;
    groupUniforms[7].values = groupMetalRough;
    const std::array<float, 4> groupingMode{
        debugMode[0], group.transforms.size() > 1U ? 1.0F : 0.0F, 0.0F, 0.0F};
    // Other internal passes use the remaining lanes for camera data.
    if (debugMode[0] == 6.F) {
      groupUniforms[2].values = groupingMode;
    }
    const std::array<float, 4> unlitAmbient{1.0F, 1.0F, 1.0F, 1.0F};
    const std::array<float, 4> noLight{};
    if (group.unlit) {
      groupMetalRough[2] = 1.0F;
      groupUniforms[3].values = noLight;
      groupUniforms[4].values = noLight;
      groupUniforms[5].values = unlitAmbient;
    }
    const bool queued =
        group.transforms.size() == 1U
            ? group.mesh->draw(commands, view, meshProgram, group.texture,
                               sampler, group.transforms.front(), state, error,
                               groupUniforms)
            : group.mesh->drawInstanced(
                  commands, view, instancedProgram, group.texture, sampler,
                  group.transforms, state, error, groupUniforms);
    if (!queued) {
      return false;
    }
    ++draws;
    triangles += group.mesh->indexCount() / 3U *
                 static_cast<std::uint32_t>(group.transforms.size());
  }
  return true;
}

} // namespace demi::runtime::render
