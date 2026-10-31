#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"
#include <span>

#include <bit>
#include <functional>
#include <stdexcept>
#include <utility>

namespace demi::runtime::render {

namespace {
bool sameOverrides(const MeshRendererComponent *a,
                   const MeshRendererComponent *b) {
  if (a == b) {
    return true;
  }
  if (!a || !b || a->materialNumbers != b->materialNumbers ||
      a->materialColors.size() != b->materialColors.size() ||
      a->metallic != b->metallic || a->roughness != b->roughness ||
      a->opacity != b->opacity || a->surfaceMode != b->surfaceMode ||
      a->wireframe != b->wireframe || a->size.x != b->size.x ||
      a->size.y != b->size.y || a->size.z != b->size.z) {
    return false;
  }
  for (const auto &[name, color] : a->materialColors) {
    const auto found = b->materialColors.find(name);
    if (found == b->materialColors.end()) {
      return false;
    }
    const auto value = found->second;
    if (color.r != value.r || color.g != value.g || color.b != value.b ||
        color.a != value.a) {
      return false;
    }
  }
  return true;
}
std::size_t overrideHash(const MeshRendererComponent *source) {
  if (!source) {
    return 0;
  }
  const auto number = [](float value) { return std::hash<float>{}(value); };
  std::size_t result = 1;
  // Unordered map insertion order must not change batching identity. Equality
  // still compares complete values, so hash collisions cannot merge materials.
  for (const auto &[name, value] : source->materialNumbers) {
    result += std::hash<std::string>{}(name) ^ (number(value) * 31);
  }
  for (const auto &[name, color] : source->materialColors) {
    result += std::hash<std::string>{}(name) ^
              (number(color.r) * 31 + number(color.g) * 131 +
               number(color.b) * 521 + number(color.a) * 2053);
  }
  for (const auto value :
       {source->metallic, source->roughness, source->opacity}) {
    result = result * 31 + (value ? number(*value) + 1 : 0);
  }
  result = result * 31 + (source->surfaceMode
                              ? std::hash<std::string>{}(*source->surfaceMode)
                              : 0);
  result = result * 31 + source->wireframe;
  for (float value : {source->size.x, source->size.y, source->size.z}) {
    result = result * 31 + number(value);
  }
  return result;
}
} // namespace

bool MeshInstanceBatch3D::Key::operator==(const Key &other) const {
  return mesh == other.mesh && texture == other.texture &&
         uniformBits == other.uniformBits && unlit == other.unlit &&
         material == other.material && surface == other.surface &&
         usesPbrSurface == other.usesPbrSurface &&
         firstIndex == other.firstIndex && indexCount == other.indexCount &&
         sameOverrides(source, other.source);
}

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
  hash ^= overrideHash(key.source);
  hash ^= std::hash<const MaterialBinding *>{}(key.material);
  combine(key.surface ? 1U : 0U);
  combine(key.usesPbrSurface ? 1U : 0U);
  combine(key.firstIndex);
  combine(key.indexCount);
  return hash;
}

std::size_t MeshInstanceBatch3D::add(const GpuMesh3D &mesh,
                                     TextureHandle texture,
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
  const auto [found, inserted] = groupLookup_.try_emplace(key, usedGroups_);
  if (inserted) {
    appendGroup({.mesh = &mesh,
                 .texture = texture,
                 .tint = tint,
                 .metalRough = metalRough,
                 .unlit = unlit});
  }
  appendTransform(found->second, transform);
  return found->second;
}

std::size_t MeshInstanceBatch3D::addSurface(
    const GpuMesh3D &mesh, TextureHandle texture,
    const MeshRendererComponent &source, const MaterialBinding *material,
    const MeshDrawSurface3D &surface, bool usesPbrSurface,
    const std::array<float, 16> &transform, std::uint32_t firstIndex,
    std::uint32_t indexCount) {
  const bool ownerOverrides =
      !source.materialNumbers.empty() || !source.materialColors.empty() ||
      source.metallic || source.roughness || source.opacity ||
      source.surfaceMode || source.wireframe;
  Key key{.mesh = &mesh,
          .texture = texture,
          .unlit = surface.unlit,
          .source = ownerOverrides ? &source : nullptr,
          .material = material,
          .surface = true,
          .usesPbrSurface = usesPbrSurface,
          .firstIndex = firstIndex,
          .indexCount = indexCount};
  // Transform/size are instance data. Distinct entities using the same plain
  // material and geometry must not become separate draw calls merely because
  // their Mesh Renderer components have different addresses.
  for (std::size_t index = 0; index < 4; ++index) {
    key.uniformBits[index] =
        std::bit_cast<std::uint32_t>(surface.builtinSurfaceTint[index]);
    key.uniformBits[index + 4] =
        std::bit_cast<std::uint32_t>(surface.metalRough[index]);
  }
  const auto [found, inserted] = groupLookup_.try_emplace(key, usedGroups_);
  if (inserted) {
    appendGroup({.mesh = &mesh,
                 .texture = texture,
                 .source = &source,
                 .material = material,
                 .surface = surface,
                 .usesPbrSurface = usesPbrSurface,
                 .firstIndex = firstIndex,
                 .indexCount = indexCount});
  }
  appendTransform(found->second, transform);
  return found->second;
}

void MeshInstanceBatch3D::appendGroup(InstanceGroup group) {
  if (usedGroups_ == groups_.size()) {
    groups_.push_back(std::move(group));
  } else {
    group.transforms.swap(groups_[usedGroups_].transforms);
    group.transforms.clear();
    groups_[usedGroups_] = std::move(group);
  }
  ++usedGroups_;
}

void MeshInstanceBatch3D::reset() {
  groupLookup_.clear();
  for (auto &group : groups_) {
    auto transforms = std::move(group.transforms);
    transforms.clear();
    group = InstanceGroup{.transforms = std::move(transforms)};
  }
  usedGroups_ = 0;
}

void MeshInstanceBatch3D::appendTransform(
    std::size_t group, const std::array<float, 16> &transform) {
  if (group >= usedGroups_) {
    throw std::out_of_range(
        "Instance group token is outside this collector operation.");
  }
  groups_[group].transforms.push_back(transform);
}

bool MeshInstanceBatch3D::flush(
    RenderCommands &commands, std::uint16_t view, ProgramHandle meshProgram,
    ProgramHandle instancedProgram, SamplerHandle sampler,
    const DrawState &state, std::span<const DrawUniformValue> lightingUniforms,
    std::uint32_t &draws, std::uint32_t &triangles, std::string &error,
    MeshPbrBindings3D *surfaces, TextureHandle whiteTexture,
    const MeshSurfacePassPrograms3D &pass) const {
  const std::array<float, 4> debugMode{lightingUniforms[2].values[0], 0, 0, 0};
  for (std::size_t index = 0; index < usedGroups_; ++index) {
    const auto &group = groups_[index];
    std::vector<DrawUniformValue> uniforms;
    std::optional<PreparedMeshDraw3D> preparedSurface;
    ProgramHandle individualProgram = meshProgram;
    ProgramHandle groupedProgram = instancedProgram;
    DrawState drawState = state;
    const std::array<float, 4> unlitAmbient{1.F, 1.F, 1.F, 1.F};
    const std::array<float, 4> noLight{};
    auto metalRough = group.metalRough;
    const std::array<float, 4> groupingMode{
        debugMode[0], group.transforms.size() > 1U ? 1.F : 0.F, 0.F, 0.F};
    if (group.surface) {
      if (!surfaces) {
        error = "Mesh surface batches require a material binding adapter.";
        return false;
      }
      if (!surfaces->prepare(group.material, *group.source, whiteTexture, 1.F,
                             error)) {
        return false;
      }
      if (group.usesPbrSurface) {
        individualProgram = pass.pbrMesh;
        groupedProgram = pass.pbrInstanced;
        if (!individualProgram ||
            (group.transforms.size() > 1 && !groupedProgram)) {
          error = "PBR instance programs are unavailable for this pass.";
          return false;
        }
      }
      preparedSurface.emplace(*group.surface, group.material, pass.depthOnly,
                              pass.overdraw, individualProgram);
      preparedSurface->fillUniforms(lightingUniforms, group.material, uniforms);
      individualProgram = preparedSurface->program;
      drawState = preparedSurface->state;
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
                               error, uniforms, group.firstIndex,
                               group.indexCount)
            : group.mesh->drawInstanced(
                  bindings, view, groupedProgram, group.texture, sampler,
                  group.transforms, drawState, error, uniforms,
                  group.firstIndex, group.indexCount);
    if (!queued) {
      return false;
    }
    ++draws;
    const auto indexCount = group.indexCount == UINT32_MAX
                                ? group.mesh->indexCount() - group.firstIndex
                                : group.indexCount;
    triangles +=
        indexCount / 3U * static_cast<std::uint32_t>(group.transforms.size());
  }
  return true;
}

} // namespace demi::runtime::render
