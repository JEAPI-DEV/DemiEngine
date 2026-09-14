#include "demi/runtime/render/bgfx3d/GpuSkinnedMesh3D.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <cmath>
#include <limits>

namespace demi::runtime::render {

bool buildGpuSkinVertices(const assets::GltfSkinnedModel3D &model,
                          std::vector<GpuSkinnedVertex3D> &vertices,
                          std::string &reason) {
  vertices.clear();
  if (model.skins.size() != 1 || model.skins[0].joints.empty() ||
      model.skins[0].joints.size() > MaximumGpuSkinJoints) {
    reason = "GPU skinning requires one skin with 1..128 joints.";
    return false;
  }
  if (model.vertices.empty() || model.indices.empty() ||
      model.indices.size() % 3) {
    reason = "GPU skinning requires indexed triangles.";
    return false;
  }
  for (auto i : model.indices) {
    if (i >= model.vertices.size()) {
      reason = "GPU skin index is outside the vertex array.";
      return false;
    }
  }
  vertices.reserve(model.vertices.size());
  for (const auto &v : model.vertices) {
    const auto n = v.normal;
    const float lengthSquared = n.x * n.x + n.y * n.y + n.z * n.z;
    if (v.skin != 0 || !std::isfinite(lengthSquared) ||
        lengthSquared < 1e-12F || !std::isfinite(v.position.x) ||
        !std::isfinite(v.position.y) || !std::isfinite(v.position.z)) {
      reason =
          "GPU skinning requires skinned vertices with valid authored normals.";
      vertices.clear();
      return false;
    }
    GpuSkinnedVertex3D output;
    output.vertex = {.x = v.position.x,
                     .y = v.position.y,
                     .z = v.position.z,
                     .nx = n.x,
                     .ny = n.y,
                     .nz = n.z,
                     .rgba = packVertexColorRgba8(v.color),
                     .u = v.uv.x,
                     .v = v.uv.y};
    float total = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      if (!std::isfinite(v.weights[i]) || v.weights[i] < 0 ||
          (v.weights[i] > 0 && v.joints[i] >= model.skins[0].joints.size())) {
        reason =
            "GPU skinning requires valid finite joint weights and indices.";
        vertices.clear();
        return false;
      }
      // Even a zero-weight shader operand must use an in-range matrix index.
      output.joints[i] = v.weights[i] > 0 ? float(v.joints[i]) : 0;
      output.weights[i] = v.weights[i];
      total += v.weights[i];
    }
    if (!(total > 0) || !std::isfinite(total)) {
      reason = "Unweighted vertices use the CPU skinning path.";
      vertices.clear();
      return false;
    }
    vertices.push_back(output);
  }
  reason.clear();
  return true;
}

GpuSkinnedMesh3D::~GpuSkinnedMesh3D() {
  if (vertices_)
    resources_.destroy(vertices_);
  if (indices_)
    resources_.destroy(indices_);
}

bool GpuSkinnedMesh3D::upload(std::span<const GpuSkinnedVertex3D> vertices,
                              std::span<const std::uint32_t> indices,
                              std::string &error) {
  if (vertices_ || indices_ || vertices.empty() || indices.empty() ||
      indices.size() % 3 || vertices.size_bytes() > UINT32_MAX ||
      indices.size_bytes() > UINT32_MAX) {
    error = "Invalid or already uploaded GPU skin geometry.";
    return false;
  }
  for (auto index : indices)
    if (index >= vertices.size()) {
      error = "GPU skin index is outside the vertex array.";
      return false;
    }
  auto layout = gpuMeshVertexLayout3D();
  layout.attributes.push_back({.semantic = VertexSemantic::Indices,
                               .components = 4,
                               .type = VertexElementType::Float});
  layout.attributes.push_back({.semantic = VertexSemantic::Weight,
                               .components = 4,
                               .type = VertexElementType::Float});
  vertices_ = resources_.createBuffer({.kind = BufferKind::Vertex,
                                       .data = std::as_bytes(vertices),
                                       .vertexLayout = layout,
                                       .debugName = "Skinned model vertices"},
                                      error);
  if (!vertices_)
    return false;
  indices_ = resources_.createBuffer({.kind = BufferKind::Index32,
                                      .data = std::as_bytes(indices),
                                      .vertexLayout = {},
                                      .debugName = "Skinned model indices"},
                                     error);
  if (!indices_) {
    resources_.destroy(vertices_);
    vertices_ = {};
    return false;
  }
  vertexCount_ = static_cast<std::uint32_t>(vertices.size());
  indexCount_ = static_cast<std::uint32_t>(indices.size());
  return true;
}

bool GpuSkinnedMesh3D::draw(RenderCommands &commands, std::uint16_t view,
                            ProgramHandle program, TextureHandle texture,
                            SamplerHandle sampler,
                            const std::array<float, 16> &transform,
                            const DrawState &state,
                            std::span<const DrawUniformValue> uniforms,
                            std::string &error) const {
  return commands.submit(
      BufferedDraw{.viewId = view,
                   .vertices = {.handle = vertices_, .count = vertexCount_},
                   .indices = {.handle = indices_, .count = indexCount_},
                   .program = program,
                   .texture = texture,
                   .sampler = sampler,
                   .state = state,
                   .scissor = {},
                   .transform = transform,
                   .uniforms = uniforms},
      error);
}

} // namespace demi::runtime::render
