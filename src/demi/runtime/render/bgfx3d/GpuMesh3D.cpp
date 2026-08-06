#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

#include <cmath>
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
  return *this;
}

bool GpuMesh3D::upload(const std::span<const Vec3> positions,
                       const std::span<const Vec2> textureCoordinates,
                       const std::span<const std::uint32_t> sourceIndices,
                       const std::uint32_t rgba, std::string &error,
                       const std::span<const Vec3> sourceNormals,
                       const std::span<const std::uint32_t> colors) {
  if (resources_ == nullptr) {
    error = "GPU mesh has no resource owner.";
    return false;
  }
  if (positions.empty()) {
    error = "GPU mesh requires at least one vertex.";
    return false;
  }
  if (!textureCoordinates.empty() &&
      textureCoordinates.size() != positions.size()) {
    error = "GPU mesh texture-coordinate count must match its vertices.";
    return false;
  }
  if (!sourceNormals.empty() && sourceNormals.size() != positions.size()) {
    error = "GPU mesh normal count must match its vertices.";
    return false;
  }
  if (!colors.empty() && colors.size() != positions.size()) {
    error = "GPU mesh color count must match its vertices.";
    return false;
  }
  std::vector<std::uint32_t> generatedIndices;
  std::span<const std::uint32_t> indices = sourceIndices;
  if (indices.empty()) {
    generatedIndices.resize(positions.size());
    std::iota(generatedIndices.begin(), generatedIndices.end(), 0U);
    indices = generatedIndices;
  }
  if (indices.empty() || indices.size() % 3U != 0U) {
    error = "GPU mesh indices must describe complete triangles.";
    return false;
  }
  for (const std::uint32_t index : indices) {
    if (index >= positions.size()) {
      error = "GPU mesh index lies outside the vertex array.";
      return false;
    }
  }

  std::vector<Vec3> generatedNormals;
  std::span<const Vec3> normals = sourceNormals;
  if (normals.empty()) {
    generatedNormals.resize(positions.size());
    for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3U) {
      const Vec3 a = positions[indices[triangle]];
      const Vec3 b = positions[indices[triangle + 1U]];
      const Vec3 c = positions[indices[triangle + 2U]];
      const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
      const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
      const Vec3 face{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z,
                      ab.x * ac.y - ab.y * ac.x};
      for (std::size_t corner = 0; corner < 3U; ++corner) {
        Vec3 &normal = generatedNormals[indices[triangle + corner]];
        normal = {normal.x + face.x, normal.y + face.y, normal.z + face.z};
      }
    }
    for (Vec3 &normal : generatedNormals) {
      const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                     normal.z * normal.z);
      normal = length > 0.000001F ? Vec3{normal.x / length, normal.y / length,
                                         normal.z / length}
                                  : Vec3{0.0F, 1.0F, 0.0F};
    }
    normals = generatedNormals;
  }

  std::vector<GpuMeshVertex3D> vertices;
  vertices.reserve(positions.size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const Vec2 uv =
        textureCoordinates.empty() ? Vec2{} : textureCoordinates[index];
    vertices.push_back({.x = positions[index].x,
                        .y = positions[index].y,
                        .z = positions[index].z,
                        .nx = normals[index].x,
                        .ny = normals[index].y,
                        .nz = normals[index].z,
                        .rgba = colors.empty() ? rgba : colors[index],
                        .u = uv.x,
                        .v = uv.y});
  }

  clear();
  vertices_ =
      resources_->createBuffer({.kind = BufferKind::Vertex,
                                .data = std::as_bytes(std::span(vertices)),
                                .vertexLayout = gpuMeshVertexLayout3D(),
                                .debugName = "3D mesh vertices"},
                               error);
  if (!vertices_)
    return false;

  const bool needs32Bit =
      positions.size() >
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
  vertexCount_ = static_cast<std::uint32_t>(positions.size());
  indexCount_ = static_cast<std::uint32_t>(indices.size());
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
}

} // namespace demi::runtime::render
