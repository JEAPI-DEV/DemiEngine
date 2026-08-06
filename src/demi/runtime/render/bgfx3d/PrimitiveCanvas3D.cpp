#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace demi::runtime::render {
namespace {

VertexLayout primitiveVertexLayout() {
  return VertexLayout{.attributes = {
                          {.semantic = VertexSemantic::Position,
                           .components = 3,
                           .type = VertexElementType::Float},
                          {.semantic = VertexSemantic::Color0,
                           .components = 4,
                           .type = VertexElementType::UInt8,
                           .normalized = true},
                      }};
}

bool positive(const Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && value.x > 0.0F && value.y > 0.0F &&
         value.z > 0.0F;
}

} // namespace

PrimitiveCanvas3D::PrimitiveCanvas3D(GpuResources &resources,
                                     RenderCommands &commands)
    : resources_(resources), commands_(commands) {}

PrimitiveCanvas3D::~PrimitiveCanvas3D() { shutdown(); }

bool PrimitiveCanvas3D::initialize(std::string &error) {
  if (program_)
    return true;
  program_ =
      resources_.createBuiltinProgram(BuiltinProgram::VertexColor3D, error);
  return static_cast<bool>(program_);
}

void PrimitiveCanvas3D::shutdown() {
  vertices_.clear();
  indices_.clear();
  lineVertices_.clear();
  lineIndices_.clear();
  frameOpen_ = false;
  statistics_ = {};
  if (program_)
    resources_.destroy(program_);
  program_ = {};
}

bool PrimitiveCanvas3D::begin(const View3DConfig &view, std::string &error) {
  if (!program_) {
    error = "PrimitiveCanvas3D must be initialized before beginning a frame.";
    return false;
  }
  if (frameOpen_) {
    error = "PrimitiveCanvas3D cannot begin twice without flushing.";
    return false;
  }
  if (!commands_.configureView3D(view, error))
    return false;
  vertices_.clear();
  indices_.clear();
  lineVertices_.clear();
  lineIndices_.clear();
  statistics_ = {};
  viewId_ = view.id;
  frameOpen_ = true;
  return true;
}

bool PrimitiveCanvas3D::append(const std::span<const Vec3> vertices,
                               const std::span<const std::uint16_t> indices,
                               const WorldTransform3D &transform,
                               const std::uint32_t rgba) {
  if (!frameOpen_ || vertices.empty() || indices.empty() ||
      vertices_.size() + vertices.size() >
          std::numeric_limits<std::uint16_t>::max())
    return false;
  for (const std::uint16_t index : indices)
    if (index >= vertices.size())
      return false;

  const auto base = static_cast<std::uint16_t>(vertices_.size());
  vertices_.reserve(vertices_.size() + vertices.size());
  for (const Vec3 local : vertices) {
    const Vec3 world = transformPoint3D(transform, local);
    vertices_.push_back(
        {.x = world.x, .y = world.y, .z = world.z, .rgba = rgba});
  }
  indices_.reserve(indices_.size() + indices.size());
  for (const std::uint16_t index : indices)
    indices_.push_back(static_cast<std::uint16_t>(base + index));
  return true;
}

bool PrimitiveCanvas3D::cube(const WorldTransform3D &transform, const Vec3 size,
                             const std::uint32_t rgba) {
  if (!positive(size))
    return false;
  const float x = size.x * 0.5F;
  const float y = size.y * 0.5F;
  const float z = size.z * 0.5F;
  const std::array<Vec3, 8> vertices{{
      {-x, -y, -z},
      {x, -y, -z},
      {x, y, -z},
      {-x, y, -z},
      {-x, -y, z},
      {x, -y, z},
      {x, y, z},
      {-x, y, z},
  }};
  constexpr std::array<std::uint16_t, 36> Indices{
      0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
      3, 7, 6, 3, 6, 2, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3,
  };
  return append(vertices, Indices, transform, rgba);
}

bool PrimitiveCanvas3D::plane(const WorldTransform3D &transform,
                              const Vec3 size, const std::uint32_t rgba) {
  if (!positive(size))
    return false;
  const float x = size.x * 0.5F;
  const float z = size.z * 0.5F;
  const std::array<Vec3, 4> vertices{{
      {-x, 0.0F, -z},
      {x, 0.0F, -z},
      {x, 0.0F, z},
      {-x, 0.0F, z},
  }};
  constexpr std::array<std::uint16_t, 6> Indices{0, 2, 1, 0, 3, 2};
  return append(vertices, Indices, transform, rgba);
}

bool PrimitiveCanvas3D::sphere(const WorldTransform3D &transform,
                               const float diameter, const std::uint32_t rgba,
                               const std::uint16_t slices,
                               const std::uint16_t stacks) {
  if (!std::isfinite(diameter) || diameter <= 0.0F || slices < 3 || stacks < 2)
    return false;
  const std::size_t vertexCount =
      static_cast<std::size_t>(slices + 1U) * (stacks + 1U);
  if (vertices_.size() + vertexCount >
      std::numeric_limits<std::uint16_t>::max())
    return false;

  std::vector<Vec3> vertices;
  std::vector<std::uint16_t> indices;
  vertices.reserve(vertexCount);
  indices.reserve(static_cast<std::size_t>(slices) * stacks * 6U);
  const float radius = diameter * 0.5F;
  for (std::uint16_t stack = 0; stack <= stacks; ++stack) {
    const float vertical = static_cast<float>(stack) / stacks;
    const float latitude = vertical * std::numbers::pi_v<float>;
    const float ringRadius = std::sin(latitude) * radius;
    const float y = std::cos(latitude) * radius;
    for (std::uint16_t slice = 0; slice <= slices; ++slice) {
      const float horizontal = static_cast<float>(slice) / slices;
      const float longitude = horizontal * std::numbers::pi_v<float> * 2.0F;
      vertices.push_back({std::cos(longitude) * ringRadius, y,
                          std::sin(longitude) * ringRadius});
    }
  }
  const std::uint16_t row = static_cast<std::uint16_t>(slices + 1U);
  for (std::uint16_t stack = 0; stack < stacks; ++stack)
    for (std::uint16_t slice = 0; slice < slices; ++slice) {
      const auto topLeft = static_cast<std::uint16_t>(stack * row + slice);
      const auto bottomLeft = static_cast<std::uint16_t>(topLeft + row);
      indices.insert(indices.end(),
                     {topLeft, bottomLeft,
                      static_cast<std::uint16_t>(topLeft + 1U),
                      static_cast<std::uint16_t>(topLeft + 1U), bottomLeft,
                      static_cast<std::uint16_t>(bottomLeft + 1U)});
    }
  return append(vertices, indices, transform, rgba);
}

bool PrimitiveCanvas3D::cylinder(const WorldTransform3D &transform,
                                 const Vec3 size, const std::uint32_t rgba,
                                 const std::uint16_t slices) {
  if (!positive(size) || slices < 3)
    return false;
  std::vector<Vec3> vertices;
  std::vector<std::uint16_t> indices;
  vertices.reserve(static_cast<std::size_t>(slices) * 2U + 2U);
  indices.reserve(static_cast<std::size_t>(slices) * 12U);
  const float radius = size.x * 0.5F;
  const float halfHeight = size.y * 0.5F;
  for (std::uint16_t slice = 0; slice < slices; ++slice) {
    const float angle =
        static_cast<float>(slice) / slices * std::numbers::pi_v<float> * 2.0F;
    const float x = std::cos(angle) * radius;
    const float z = std::sin(angle) * radius;
    vertices.push_back({x, -halfHeight, z});
    vertices.push_back({x, halfHeight, z});
  }
  const auto bottomCenter = static_cast<std::uint16_t>(vertices.size());
  vertices.push_back({0.0F, -halfHeight, 0.0F});
  const auto topCenter = static_cast<std::uint16_t>(vertices.size());
  vertices.push_back({0.0F, halfHeight, 0.0F});
  for (std::uint16_t slice = 0; slice < slices; ++slice) {
    const auto next = static_cast<std::uint16_t>((slice + 1U) % slices);
    const auto bottom = static_cast<std::uint16_t>(slice * 2U);
    const auto top = static_cast<std::uint16_t>(bottom + 1U);
    const auto nextBottom = static_cast<std::uint16_t>(next * 2U);
    const auto nextTop = static_cast<std::uint16_t>(nextBottom + 1U);
    indices.insert(indices.end(),
                   {bottom, nextBottom, top, top, nextBottom, nextTop,
                    bottomCenter, bottom, nextBottom, topCenter, nextTop, top});
  }
  return append(vertices, indices, transform, rgba);
}

bool PrimitiveCanvas3D::triangles(const WorldTransform3D &transform,
                                  const std::span<const Vec3> vertices,
                                  const std::uint32_t rgba) {
  if (vertices.empty() || vertices.size() % 3U != 0U ||
      vertices.size() > std::numeric_limits<std::uint16_t>::max())
    return false;
  std::vector<std::uint16_t> indices(vertices.size());
  for (std::size_t index = 0; index < vertices.size(); ++index)
    indices[index] = static_cast<std::uint16_t>(index);
  return append(vertices, indices, transform, rgba);
}

bool PrimitiveCanvas3D::line(const Vec3 start, const Vec3 end,
                             const std::uint32_t rgba) {
  if (!frameOpen_ ||
      lineVertices_.size() + 2U > std::numeric_limits<std::uint16_t>::max())
    return false;
  const auto base = static_cast<std::uint16_t>(lineVertices_.size());
  lineVertices_.push_back(
      {.x = start.x, .y = start.y, .z = start.z, .rgba = rgba});
  lineVertices_.push_back({.x = end.x, .y = end.y, .z = end.z, .rgba = rgba});
  lineIndices_.push_back(base);
  lineIndices_.push_back(static_cast<std::uint16_t>(base + 1U));
  return true;
}

bool PrimitiveCanvas3D::flush(std::string &error) {
  if (!frameOpen_) {
    error = "PrimitiveCanvas3D has no open frame to flush.";
    return false;
  }
  frameOpen_ = false;
  if (vertices_.empty() && lineVertices_.empty())
    return true;
  std::uint32_t drawCalls = 0;
  if (!vertices_.empty()) {
    const TransientDraw draw{
        .viewId = viewId_,
        .vertices = std::as_bytes(std::span(vertices_)),
        .vertexLayout = primitiveVertexLayout(),
        .indices = indices_,
        .program = program_,
        .texture = {},
        .sampler = {},
        .blend = BlendMode::Opaque,
        .state = {.blend = BlendMode::Opaque,
                  .depthTest = DepthTest::Less,
                  .cull = CullMode::None,
                  .topology = PrimitiveTopology::Triangles,
                  .writeDepth = true},
        .scissor = {},
        .uniforms = {},
    };
    if (!commands_.submit(draw, error))
      return false;
    ++drawCalls;
  }
  if (!lineVertices_.empty()) {
    const TransientDraw draw{
        .viewId = viewId_,
        .vertices = std::as_bytes(std::span(lineVertices_)),
        .vertexLayout = primitiveVertexLayout(),
        .indices = lineIndices_,
        .program = program_,
        .texture = {},
        .sampler = {},
        .blend = BlendMode::Alpha,
        .state = {.blend = BlendMode::Alpha,
                  .depthTest = DepthTest::LessEqual,
                  .cull = CullMode::None,
                  .topology = PrimitiveTopology::Lines,
                  .writeDepth = false},
        .scissor = {},
        .uniforms = {},
    };
    if (!commands_.submit(draw, error))
      return false;
    ++drawCalls;
  }
  statistics_ = {
      .drawCalls = drawCalls,
      .vertices =
          static_cast<std::uint32_t>(vertices_.size() + lineVertices_.size()),
      .indices =
          static_cast<std::uint32_t>(indices_.size() + lineIndices_.size()),
      .triangles = static_cast<std::uint32_t>(indices_.size() / 3U),
  };
  return true;
}

} // namespace demi::runtime::render
