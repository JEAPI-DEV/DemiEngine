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
  hash ^= std::hash<const MeshRendererComponent *>{}(key.source);
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

std::size_t MeshInstanceBatch3D::addSurface(
    const GpuMesh3D &mesh, TextureHandle texture,
    const MeshRendererComponent &source, const MaterialBinding *material,
    const PreparedMeshDraw3D &prepared, ProgramHandle instancedProgram,
    const std::array<float, 16> &transform) {
  // A source has one resolved surface per pass. Different LOD geometry still
  // receives its own key, while copies of the same source share all bindings.
  const Key key{.mesh = &mesh, .texture = texture, .source = &source};
  const auto [found, inserted] = groupLookup_.try_emplace(key, groups_.size());
  if (inserted) {
    groups_.push_back({.mesh = &mesh,
                       .texture = texture,
                       .source = &source,
                       .material = material,
                       .prepared = prepared,
                       .instancedProgram = instancedProgram});
  }
  appendTransform(found->second, transform);
  return found->second;
}

void MeshInstanceBatch3D::appendTransform(
    std::size_t group, const std::array<float, 16> &transform) {
  groups_.at(group).transforms.push_back(transform);
}

bool MeshInstanceBatch3D::flush(
    RenderCommands &commands, std::uint16_t view, ProgramHandle meshProgram,
    ProgramHandle instancedProgram, SamplerHandle sampler,
    const DrawState &state,
    const std::array<DrawUniformValue, 8> &lightingUniforms,
    std::uint32_t &draws, std::uint32_t &triangles, std::string &error,
    MeshPbrBindings3D *surfaces, TextureHandle whiteTexture) const {
  const std::array<float, 4> debugMode{lightingUniforms[2].values[0], 0, 0, 0};
  for (const auto &group : groups_) {
    std::vector<DrawUniformValue> uniforms;
    ProgramHandle individualProgram = meshProgram;
    ProgramHandle groupedProgram = instancedProgram;
    DrawState drawState = state;
    const std::array<float, 4> unlitAmbient{1.F, 1.F, 1.F, 1.F};
    const std::array<float, 4> noLight{};
    auto metalRough = group.metalRough;
    const std::array<float, 4> groupingMode{
        debugMode[0], group.transforms.size() > 1U ? 1.F : 0.F, 0.F, 0.F};
    if (group.prepared) {
      if (!surfaces) {
        error = "Mesh surface batches require a material binding adapter.";
        return false;
      }
      if (!surfaces->prepare(group.material, *group.source, whiteTexture, 1.F,
                             error)) {
        return false;
      }
      group.prepared->fillUniforms(lightingUniforms, group.material, uniforms);
      individualProgram = group.prepared->program;
      groupedProgram = group.instancedProgram;
      drawState = group.prepared->state;
    } else {
      if (surfaces) {
        surfaces->reset();
      }
      uniforms.assign(lightingUniforms.begin(), lightingUniforms.end());
      uniforms.front().values = group.tint;
      uniforms[7].values = metalRough;
      if (group.unlit) {
        metalRough[2] = 1.F;
        uniforms[3].values = noLight;
        uniforms[4].values = noLight;
        uniforms[5].values = unlitAmbient;
      }
    }
    // The remaining debug lanes carry camera direction in water depth capture.
    if (debugMode[0] == 6.F) {
      uniforms[2].values = groupingMode;
    }
    RenderCommands &bindings =
        surfaces ? static_cast<RenderCommands &>(*surfaces) : commands;
    const bool queued =
        group.transforms.size() == 1U
            ? group.mesh->draw(bindings, view, individualProgram, group.texture,
                               sampler, group.transforms.front(), drawState,
                               error, uniforms)
            : group.mesh->drawInstanced(
                  bindings, view, groupedProgram, group.texture, sampler,
                  group.transforms, drawState, error, uniforms);
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
