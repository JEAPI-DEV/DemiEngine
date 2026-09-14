#pragma once

#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

namespace demi::runtime::render {

inline constexpr std::uint16_t MaximumGpuSkinJoints = 128;

struct GpuSkinnedVertex3D {
  GpuMeshVertex3D vertex;
  std::array<float, 4> joints{};
  std::array<float, 4> weights{};
};

// Unsupported models keep the CPU reference path. No new authored asset type.
[[nodiscard]] bool
buildGpuSkinVertices(const assets::GltfSkinnedModel3D &model,
                     std::vector<GpuSkinnedVertex3D> &vertices,
                     std::string &reason);

class GpuSkinnedMesh3D {
public:
  explicit GpuSkinnedMesh3D(GpuResources &resources) : resources_(resources) {}
  ~GpuSkinnedMesh3D();
  GpuSkinnedMesh3D(const GpuSkinnedMesh3D &) = delete;
  GpuSkinnedMesh3D &operator=(const GpuSkinnedMesh3D &) = delete;
  [[nodiscard]] bool upload(std::span<const GpuSkinnedVertex3D> vertices,
                            std::span<const std::uint32_t> indices,
                            std::string &error);
  [[nodiscard]] bool
  draw(RenderCommands &commands, std::uint16_t view, ProgramHandle program,
       TextureHandle texture, SamplerHandle sampler,
       const std::array<float, 16> &transform, const DrawState &state,
       std::span<const DrawUniformValue> uniforms, std::string &error) const;
  [[nodiscard]] std::uint32_t indexCount() const { return indexCount_; }

private:
  GpuResources &resources_;
  BufferHandle vertices_, indices_;
  std::uint32_t vertexCount_ = 0, indexCount_ = 0;
};

} // namespace demi::runtime::render
