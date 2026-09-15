#include "demi/runtime/render/bgfx3d/GpuSkinnedMesh3D.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <cmath>
#include <map>
#include <tuple>

namespace demi::runtime::render {

bool buildGpuSkinVertices(const assets::GltfSkinnedModel3D &model,
                          std::vector<GpuSkinnedVertex3D> &vertices,
                          GpuSkinPaletteLayout &layout, std::string &reason) {
  vertices.clear();
  layout.rows.clear();
  const auto reject = [&](const char *message) {
    vertices.clear();
    layout.rows.clear();
    reason = message;
    return false;
  };
  if (model.skins.empty() && model.clips.empty())
    return reject("Static models do not need GPU skinning.");
  if (model.vertices.empty() || model.indices.empty() ||
      model.indices.size() % 3)
    return reject("GPU skinning requires indexed triangles.");
  for (auto index : model.indices)
    if (index >= model.vertices.size())
      return reject("GPU skin index is outside the vertex array.");
  for (const auto &skin : model.skins) {
    if (skin.joints.size() != skin.inverseBindMatrices.size())
      return reject("Skin joints and inverse binds do not match.");
    for (int node : skin.joints)
      if (node < 0 || static_cast<std::size_t>(node) >= model.nodes.size())
        return reject("Skin references an invalid joint node.");
  }

  using Kind = GpuSkinPaletteLayout::Kind;
  using Key = std::tuple<Kind, std::size_t, std::size_t>;
  std::map<Key, std::size_t> rows;
  const auto matrixIndex = [&](Kind kind, std::size_t skin,
                               std::size_t index) -> int {
    const Key key{kind, skin, index};
    if (const auto found = rows.find(key); found != rows.end())
      return static_cast<int>(found->second);
    if (layout.rows.size() == MaximumGpuSkinMatrices)
      return -1;
    const auto slot = layout.rows.size();
    layout.rows.push_back({kind, skin, index});
    rows.emplace(key, slot);
    return static_cast<int>(slot);
  };
  vertices.reserve(model.vertices.size());
  for (const auto &v : model.vertices) {
    const auto n = v.normal;
    const float lengthSquared = n.x * n.x + n.y * n.y + n.z * n.z;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-12F ||
        !std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
        !std::isfinite(v.position.z))
      return reject(
          "GPU skinning requires valid positions and authored normals.");
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
    if (v.skin >= 0) {
      const auto skin = static_cast<std::size_t>(v.skin);
      if (skin >= model.skins.size())
        return reject("Vertex references an invalid skin.");
      for (std::size_t i = 0; i < 4; ++i) {
        if (!std::isfinite(v.weights[i]) || v.weights[i] < 0 ||
            (v.weights[i] > 0 &&
             v.joints[i] >= model.skins[skin].joints.size()))
          return reject(
              "GPU skinning requires valid finite joint weights and indices.");
        if (v.weights[i] > 0) {
          const int row = matrixIndex(Kind::SkinJoint, skin, v.joints[i]);
          if (row < 0)
            return reject("GPU skinning exceeds 128 referenced matrices.");
          output.joints[i] = static_cast<float>(row);
          output.weights[i] = v.weights[i];
          total += v.weights[i];
        }
      }
      if (!std::isfinite(total))
        return reject("GPU skin weight sum is invalid.");
    }
    if (v.skin < 0 || total == 0) {
      // CPU reference: rigid vertices follow their owner node; zero-weight
      // skinned vertices stay in local space, regardless of their owner.
      const bool useNode = v.skin < 0 && v.node >= 0;
      if (useNode && static_cast<std::size_t>(v.node) >= model.nodes.size())
        return reject("Rigid vertex references an invalid owner node.");
      const int row =
          matrixIndex(useNode ? Kind::Node : Kind::Identity, 0,
                      useNode ? static_cast<std::size_t>(v.node) : 0);
      if (row < 0)
        return reject("GPU skinning exceeds 128 referenced matrices.");
      output.joints[0] = static_cast<float>(row);
      output.weights[0] = 1;
    }
    // Inactive influences keep index zero, always a valid row after encoding.
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
                              GpuSkinPaletteLayout layout, std::string &error) {
  if (layout.rows.empty() || layout.rows.size() > MaximumGpuSkinMatrices ||
      vertices_ || indices_ || vertices.empty() || indices.empty() ||
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
  for (const auto &vertex : vertices)
    for (float joint : vertex.joints)
      if (!std::isfinite(joint) || joint < 0 || joint >= layout.rows.size() ||
          std::floor(joint) != joint) {
        error = "GPU vertex references an invalid palette row.";
        return false;
      }
  auto vertexLayout = gpuMeshVertexLayout3D();
  vertexLayout.attributes.push_back({.semantic = VertexSemantic::Indices,
                                     .components = 4,
                                     .type = VertexElementType::Float});
  vertexLayout.attributes.push_back({.semantic = VertexSemantic::Weight,
                                     .components = 4,
                                     .type = VertexElementType::Float});
  vertices_ = resources_.createBuffer({.kind = BufferKind::Vertex,
                                       .data = std::as_bytes(vertices),
                                       .vertexLayout = vertexLayout,
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
  layout_ = std::move(layout);
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
