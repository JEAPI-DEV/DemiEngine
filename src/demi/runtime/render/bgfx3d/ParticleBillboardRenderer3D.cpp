#include "demi/runtime/render/bgfx3d/ParticleBillboardRenderer3D.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace demi::runtime::render {
namespace {

struct ParticleVertex3D {
  float x;
  float y;
  float z;
  std::uint32_t rgba;
  float u;
  float v;
};

VertexLayout particleVertexLayout() {
  return VertexLayout{.attributes = {
                          {.semantic = VertexSemantic::Position,
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

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 normalized(const Vec3 value) {
  const float length = std::sqrt(dot(value, value));
  return length > 0.00001F
             ? Vec3{value.x / length, value.y / length, value.z / length}
             : Vec3{};
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

constexpr std::size_t maxQuadsPerDraw = 16'384U;

Vec3 add(const Vec3 position, const Vec3 right, const float x, const Vec3 up,
         const float y) {
  return {position.x + right.x * x + up.x * y,
          position.y + right.y * x + up.y * y,
          position.z + right.z * x + up.z * y};
}

} // namespace

ParticleBillboardRenderer3D::ParticleBillboardRenderer3D(
    GpuResources &resources, RenderCommands &commands)
    : resources_(resources), commands_(commands) {}

ParticleBillboardRenderer3D::~ParticleBillboardRenderer3D() { shutdown(); }

bool ParticleBillboardRenderer3D::initialize(std::string &error) {
  if (program_)
    return true;
  program_ = resources_.createBuiltinProgram(BuiltinProgram::Textured3D, error);
  sampler_ = resources_.createSampler("s_texColor", error);
  if (!program_ || !sampler_) {
    shutdown();
    return false;
  }
  return true;
}

void ParticleBillboardRenderer3D::shutdown() {
  if (sampler_)
    resources_.destroy(sampler_);
  if (program_)
    resources_.destroy(program_);
  sampler_ = {};
  program_ = {};
  statistics_ = {};
}

bool ParticleBillboardRenderer3D::draw(
    const std::uint16_t viewId, const BgfxCameraFrame3D &camera,
    const std::span<const ParticleBillboardDraw3D> particles,
    std::string &error) {
  statistics_ = {};
  if (!program_ || !sampler_) {
    error = "ParticleBillboardRenderer3D must be initialized before drawing.";
    return false;
  }
  if (particles.empty())
    return true;
  Vec3 forward = normalized(camera.forward);
  if (dot(forward, forward) < 0.5F)
    forward = {0.0F, 0.0F, -1.0F};
  Vec3 cameraRight = normalized(cross(forward, camera.up));
  if (dot(cameraRight, cameraRight) < 0.5F) {
    const Vec3 fallbackUp = std::abs(forward.y) < 0.99F
                                ? Vec3{0.0F, 1.0F, 0.0F}
                                : Vec3{1.0F, 0.0F, 0.0F};
    cameraRight = normalized(cross(forward, fallbackUp));
  }
  const Vec3 cameraUp = normalized(cross(cameraRight, forward));

  std::size_t first = 0;
  while (first < particles.size()) {
    const TextureHandle texture = particles[first].texture;
    const BlendMode blend = particles[first].blend;
    std::size_t end = first + 1U;
    while (end < particles.size() && end - first < maxQuadsPerDraw &&
           particles[end].texture == texture && particles[end].blend == blend)
      ++end;

    std::vector<ParticleVertex3D> vertices;
    std::vector<std::uint16_t> indices;
    vertices.reserve((end - first) * 4U);
    indices.reserve((end - first) * 6U);
    for (std::size_t index = first; index < end; ++index) {
      const ParticleRenderData3D &particle = particles[index].particle;
      const float angle = particle.rotation * 0.017453292519943295F;
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      const Vec3 right{cameraRight.x * cosine + cameraUp.x * sine,
                       cameraRight.y * cosine + cameraUp.y * sine,
                       cameraRight.z * cosine + cameraUp.z * sine};
      const Vec3 up{-cameraRight.x * sine + cameraUp.x * cosine,
                    -cameraRight.y * sine + cameraUp.y * cosine,
                    -cameraRight.z * sine + cameraUp.z * cosine};
      const float halfSize = particle.size * 0.5F;
      const std::array<Vec3, 4> corners{
          add(particle.position, right, -halfSize, up, halfSize),
          add(particle.position, right, halfSize, up, halfSize),
          add(particle.position, right, halfSize, up, -halfSize),
          add(particle.position, right, -halfSize, up, -halfSize)};
      const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
      const std::uint32_t rgba = packVertexColorRgba8(particle.color);
      vertices.insert(
          vertices.end(),
          {{corners[0].x, corners[0].y, corners[0].z, rgba, 0.0F, 0.0F},
           {corners[1].x, corners[1].y, corners[1].z, rgba, 1.0F, 0.0F},
           {corners[2].x, corners[2].y, corners[2].z, rgba, 1.0F, 1.0F},
           {corners[3].x, corners[3].y, corners[3].z, rgba, 0.0F, 1.0F}});
      indices.insert(indices.end(), {static_cast<std::uint16_t>(base),
                                     static_cast<std::uint16_t>(base + 1U),
                                     static_cast<std::uint16_t>(base + 2U),
                                     static_cast<std::uint16_t>(base),
                                     static_cast<std::uint16_t>(base + 2U),
                                     static_cast<std::uint16_t>(base + 3U)});
    }
    if (!commands_.submit({.viewId = viewId,
                           .vertices = std::as_bytes(std::span(vertices)),
                           .vertexLayout = particleVertexLayout(),
                           .indices = indices,
                           .program = program_,
                           .texture = texture,
                           .sampler = sampler_,
                           .blend = blend,
                           .state = {.blend = blend,
                                     .depthTest = DepthTest::LessEqual,
                                     .cull = CullMode::None,
                                     .writeDepth = false},
                           .scissor = {},
                           .uniforms = {}},
                          error))
      return false;
    ++statistics_.drawCalls;
    statistics_.particles += static_cast<std::uint32_t>(end - first);
    statistics_.triangles += static_cast<std::uint32_t>((end - first) * 2U);
    first = end;
  }
  return true;
}

} // namespace demi::runtime::render
