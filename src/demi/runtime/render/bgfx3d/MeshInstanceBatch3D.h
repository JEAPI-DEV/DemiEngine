#pragma once

#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/render/bgfx3d/MeshPbrBindings3D.h"
#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

class MeshInstanceBatch3D {
public:
  std::size_t add(const GpuMesh3D &mesh, TextureHandle texture,
                  const std::array<float, 4> &tint,
                  const std::array<float, 4> &metalRough, bool unlit,
                  const std::array<float, 16> &transform);
  // Plain surfaces can share a batch across entities. Owner-specific material
  // overrides retain separate groups so their binding values cannot leak.
  std::size_t addSurface(const GpuMesh3D &mesh, TextureHandle texture,
                         const MeshRendererComponent &source,
                         const MaterialBinding *material,
                         const PreparedMeshDraw3D &prepared,
                         ProgramHandle instancedProgram,
                         const std::array<float, 16> &transform);
  // Tokens are local to this view's batch collector. Repeated immutable source
  // copies can append without resolving or hashing their surface again.
  void appendTransform(std::size_t group,
                       const std::array<float, 16> &transform);
  bool flush(RenderCommands &commands, std::uint16_t view,
             ProgramHandle meshProgram, ProgramHandle instancedProgram,
             SamplerHandle sampler, const DrawState &state,
             const std::array<DrawUniformValue, 8> &lightingUniforms,
             std::uint32_t &draws, std::uint32_t &triangles, std::string &error,
             MeshPbrBindings3D *surfaces = nullptr,
             TextureHandle whiteTexture = {}) const;

private:
  struct InstanceGroup {
    const GpuMesh3D *mesh = nullptr;
    TextureHandle texture;
    std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> metalRough{0.0F, 0.8F, 0.0F, 0.0F};
    bool unlit = false;
    const MeshRendererComponent *source = nullptr;
    const MaterialBinding *material = nullptr;
    std::optional<PreparedMeshDraw3D> prepared;
    ProgramHandle instancedProgram;
    std::vector<std::array<float, 16>> transforms;
  };
  struct Key {
    const GpuMesh3D *mesh = nullptr;
    TextureHandle texture;
    std::array<std::uint32_t, 8> uniformBits{};
    bool unlit = false;
    const MeshRendererComponent *source = nullptr;
    const MaterialBinding *material = nullptr;
    bool surface = false;
    bool operator==(const Key &) const = default;
  };
  struct KeyHash {
    std::size_t operator()(const Key &key) const;
  };
  // Programs and raster state are common to a flush. Mesh addresses are used
  // only during this view, while renderer caches retain their GPU resources.
  std::unordered_map<Key, std::size_t, KeyHash> groupLookup_;
  std::vector<InstanceGroup> groups_;
};

} // namespace demi::runtime::render
