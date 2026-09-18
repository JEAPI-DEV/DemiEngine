#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/render/bgfx3d/MeshVertexPreparation3D.h"

#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace demi::runtime::render {

VertexLayout gpuMeshVertexLayout3D() {
  return {.attributes = {
              {.semantic = VertexSemantic::Position,
               .components = 3,
               .type = VertexElementType::Float},
              {.semantic = VertexSemantic::Normal,
               .components = 3,
               .type = VertexElementType::Float},
              {.semantic = VertexSemantic::Color0,
               .components = 4,
               .type = VertexElementType::UInt8,
               .normalized = true},
              {.semantic = VertexSemantic::TexCoord0,
               .components = 2,
               .type = VertexElementType::Float},
          }};
}

GpuMesh3D::GpuMesh3D(GpuResources &resources) : resources_(&resources) {}

GpuMesh3D::~GpuMesh3D() { clear(); }

GpuMesh3D::GpuMesh3D(GpuMesh3D &&other) noexcept { *this = std::move(other); }

GpuMesh3D &GpuMesh3D::operator=(GpuMesh3D &&other) noexcept {
  if (this == &other)
    return *this;
  clear();
  resources_ = std::exchange(other.resources_, nullptr);
  vertices_ = std::exchange(other.vertices_, {});
  indices_ = std::exchange(other.indices_, {});
  vertexCount_ = std::exchange(other.vertexCount_, 0);
  indexCount_ = std::exchange(other.indexCount_, 0);
  dynamicVertices_ = std::exchange(other.dynamicVertices_, false);
  return *this;
}

bool GpuMesh3D::upload(const std::span<const Vec3> positions,
                       const std::span<const Vec2> textureCoordinates,
                       const std::span<const std::uint32_t> sourceIndices,
                       const std::uint32_t rgba, std::string &error,
                       const std::span<const Vec3> sourceNormals,
                       const std::span<const std::uint32_t> colors,
                       const bool dynamicVertices) {
  if (resources_ == nullptr) {
    error = "GPU mesh has no resource owner.";
    return false;
  }
  std::vector<std::uint32_t> generatedIndices;
  std::span<const std::uint32_t> indices = sourceIndices;
  if (indices.empty()) {
    generatedIndices.resize(positions.size());
    std::iota(generatedIndices.begin(), generatedIndices.end(), 0U);
    indices = generatedIndices;
  }
  std::vector<GpuMeshVertex3D> vertices;
  if (!prepareMeshVertices3D(positions, textureCoordinates, indices, rgba,
                             sourceNormals, colors, vertices, error))
    return false;
  return uploadPrepared(vertices, indices, error, dynamicVertices);
}

bool GpuMesh3D::uploadPrepared(const std::span<const GpuMeshVertex3D> vertices,
                               const std::span<const std::uint32_t> indices,
                               std::string &error, const bool dynamicVertices) {
  if (!resources_ || vertices.empty() || indices.empty() || indices.size() % 3) {
    error = "Invalid prepared GPU mesh.";
    return false;
  }
  for (const auto index : indices) {
    if (index >= vertices.size()) {
      error = "Prepared GPU mesh index lies outside the vertex array.";
      return false;
    }
  }
  if (dynamicVertices && dynamicVertices_ && vertexCount_ == vertices.size() &&
      indexCount_ == indices.size()) {
    return resources_->updateBuffer(vertices_,
                                    std::as_bytes(std::span(vertices)), error);
  }

  clear();
  vertices_ = resources_->createBuffer(
      {.kind = dynamicVertices ? BufferKind::DynamicVertex : BufferKind::Vertex,
       .data = std::as_bytes(std::span(vertices)),
       .vertexLayout = gpuMeshVertexLayout3D(),
       .debugName = "3D mesh vertices"},
      error);
  if (!vertices_)
    return false;

  const bool needs32Bit =
      vertices.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());
  if (needs32Bit) {
    indices_ = resources_->createBuffer({.kind = BufferKind::Index32,
                                         .data = std::as_bytes(indices),
                                         .vertexLayout = {},
                                         .debugName = "3D mesh indices"},
                                        error);
  } else {
    std::vector<std::uint16_t> indices16;
    indices16.reserve(indices.size());
    for (const std::uint32_t index : indices)
      indices16.push_back(static_cast<std::uint16_t>(index));
    indices_ =
        resources_->createBuffer({.kind = BufferKind::Index16,
                                  .data = std::as_bytes(std::span(indices16)),
                                  .vertexLayout = {},
                                  .debugName = "3D mesh indices"},
                                 error);
  }
  if (!indices_) {
    resources_->destroy(vertices_);
    vertices_ = {};
    return false;
  }
  vertexCount_ = static_cast<std::uint32_t>(vertices.size());
  indexCount_ = static_cast<std::uint32_t>(indices.size());
  dynamicVertices_ = dynamicVertices;
  return true;
}

bool GpuMesh3D::draw(RenderCommands &commands, const std::uint16_t viewId,
                     const ProgramHandle program, const TextureHandle texture,
                     const SamplerHandle sampler,
                     const std::array<float, 16> &transform,
                     const DrawState &state, std::string &error,
                     const std::span<const DrawUniformValue> uniforms) const {
  if (!valid()) {
    error = "GPU mesh must be uploaded before drawing.";
    return false;
  }
  return commands.submit(
      BufferedDraw{
          .viewId = viewId,
          .vertices = {.handle = vertices_, .count = vertexCount_},
          .indices = {.handle = indices_, .count = indexCount_},
          .program = program,
          .texture = texture,
          .sampler = sampler,
          .state = state,
          .scissor = {},
          .transform = transform,
          .uniforms = uniforms,
      },
      error);
}

bool GpuMesh3D::drawInstanced(
    RenderCommands &commands, const std::uint16_t viewId,
    const ProgramHandle program, const TextureHandle texture,
    const SamplerHandle sampler,
    const std::span<const std::array<float, 16>> transforms,
    const DrawState &state, std::string &error,
    const std::span<const DrawUniformValue> uniforms) const {
  if (!valid()) {
    error = "GPU mesh must be uploaded before drawing.";
    return false;
  }
  return commands.submit(
      InstancedBufferedDraw{
          .viewId = viewId,
          .vertices = {.handle = vertices_, .count = vertexCount_},
          .indices = {.handle = indices_, .count = indexCount_},
          .program = program,
          .texture = texture,
          .sampler = sampler,
          .state = state,
          .scissor = {},
          .transforms = transforms,
          .uniforms = uniforms,
      },
      error);
}

void GpuMesh3D::clear() {
  if (resources_ != nullptr) {
    if (vertices_)
      resources_->destroy(vertices_);
    if (indices_)
      resources_->destroy(indices_);
  }
  vertices_ = {};
  indices_ = {};
  vertexCount_ = 0;
  indexCount_ = 0;
  dynamicVertices_ = false;
}

} // namespace demi::runtime::render
