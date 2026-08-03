#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <span>
#include <string>

namespace demi::runtime::render {

struct GpuMeshVertex3D {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float nx = 0.0F;
  float ny = 1.0F;
  float nz = 0.0F;
  std::uint32_t rgba = 0xffffffffU;
  float u = 0.0F;
  float v = 0.0F;
};

// Owns immutable GPU geometry. Entity transforms and presentation resources
// remain draw-time inputs so one uploaded model can serve many entities.
class GpuMesh3D {
public:
  explicit GpuMesh3D(GpuResources &resources);
  ~GpuMesh3D();

  GpuMesh3D(const GpuMesh3D &) = delete;
  GpuMesh3D &operator=(const GpuMesh3D &) = delete;
  GpuMesh3D(GpuMesh3D &&other) noexcept;
  GpuMesh3D &operator=(GpuMesh3D &&other) noexcept;

  [[nodiscard]] bool upload(std::span<const Vec3> positions,
                            std::span<const Vec2> textureCoordinates,
                            std::span<const std::uint32_t> indices,
                            std::uint32_t rgba, std::string &error,
                            std::span<const Vec3> normals = {},
                            std::span<const std::uint32_t> colors = {});
  [[nodiscard]] bool
  draw(RenderCommands &commands, std::uint16_t viewId, ProgramHandle program,
       TextureHandle texture, SamplerHandle sampler,
       const std::array<float, 16> &transform, const DrawState &state,
       std::string &error,
       std::span<const DrawUniformValue> uniforms = {}) const;
  [[nodiscard]] bool
  drawInstanced(RenderCommands &commands, std::uint16_t viewId,
                ProgramHandle program, TextureHandle texture,
                SamplerHandle sampler,
                std::span<const std::array<float, 16>> transforms,
                const DrawState &state, std::string &error,
                std::span<const DrawUniformValue> uniforms = {}) const;
  void clear();

  [[nodiscard]] bool valid() const {
    return static_cast<bool>(vertices_) && static_cast<bool>(indices_);
  }
  [[nodiscard]] std::uint32_t vertexCount() const { return vertexCount_; }
  [[nodiscard]] std::uint32_t indexCount() const { return indexCount_; }

private:
  GpuResources *resources_ = nullptr;
  BufferHandle vertices_;
  BufferHandle indices_;
  std::uint32_t vertexCount_ = 0;
  std::uint32_t indexCount_ = 0;
};

[[nodiscard]] VertexLayout gpuMeshVertexLayout3D();

} // namespace demi::runtime::render
