#include "demi/runtime/render/bgfx3d/DirectionalShadow3D.h"
#include <cmath>
#include <vector>

namespace demi::runtime::render {
namespace {
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 unit(Vec3 a) {
  const float n = std::sqrt(dot(a, a));
  return n > 1e-6F ? Vec3{a.x / n, a.y / n, a.z / n} : Vec3{};
}
} // namespace
bool DirectionalShadow3D::initialize(std::string &error) {
  if (fallback_)
    return true;
  sampler_ = resources_.createSampler("s_shadowMap", error);
  const char *names[] = {"u_shadowX", "u_shadowY", "u_shadowZ",
                         "u_shadowParams"};
  for (int i = 0; i < 4; ++i)
    uniforms_[i] =
        resources_.createUniform(names[i], UniformType::Vec4, 1, error);
  const std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                       std::byte{255}, std::byte{255}};
  fallback_ = resources_.createTexture({.data = white,
                                        .filter = TextureFilter::Nearest,
                                        .debugName = "shadow fallback"},
                                       error);
  if (!fallback_ || !sampler_ || !uniforms_[0] || !uniforms_[1] ||
      !uniforms_[2] || !uniforms_[3]) {
    shutdown();
    return false;
  }
  sampled_ = fallback_;
  return true;
}
void DirectionalShadow3D::clearTargets() {
  for (auto &[id, target] : targets_) {
    if (target.handles.frameBuffer)
      resources_.destroy(target.handles.frameBuffer);
    if (target.handles.depth)
      resources_.destroy(target.handles.depth);
    if (target.handles.color)
      resources_.destroy(target.handles.color);
  }
  targets_.clear();
  frame_.reset();
  sampled_ = fallback_;
  values_ = {};
}
void DirectionalShadow3D::shutdown() {
  clearTargets();
  if (fallback_)
    resources_.destroy(fallback_);
  if (sampler_)
    resources_.destroy(sampler_);
  for (auto uniform : uniforms_)
    if (uniform)
      resources_.destroy(uniform);
  fallback_ = {};
  sampled_ = {};
  sampler_ = {};
  uniforms_ = {};
}
bool DirectionalShadow3D::prepare(const SceneLighting3D &light,
                                  const BgfxCameraFrame3D &camera,
                                  std::string &error) {
  frame_.reset();
  values_ = {};
  sampled_ = fallback_;
  if (!light.castsShadows || light.shadowBudget == 0 ||
      light.shadowDistance == 0 || !camera.updateContent)
    return true;
  const auto caps = resources_.limits();
  if (light.shadowResolution < 1 || light.shadowResolution > 65535 ||
      (std::uint32_t(light.shadowResolution) > caps.maxTextureSize ||
       camera.viewId + 4U >= caps.maxViews)) {
    error =
        "Directional shadow resolution or camera views exceed device limits";
    return false;
  }
  const Vec3 direction =
      unit({light.direction[0], light.direction[1], light.direction[2]});
  if (dot(direction, direction) < .5F ||
      !std::isfinite(dot(direction, direction)) ||
      !std::isfinite(light.shadowDistance * 8.F) || light.shadowDistance <= 0 ||
      !std::isfinite(1.F / light.shadowDistance) ||
      !std::isfinite(light.shadowBias) || light.shadowBias < 0) {
    error = "Invalid directional shadow direction, distance or bias";
    return false;
  }
  const auto key =
      camera.cameraId.empty() ? std::to_string(camera.viewId) : camera.cameraId;
  auto &target = targets_[key];
  if (target.resolution != light.shadowResolution) {
    const auto replacement = resources_.createRenderTarget(
        {.width = std::uint16_t(light.shadowResolution),
         .height = std::uint16_t(light.shadowResolution),
         .debugName = "directional shadow: " + key,
         .filter = TextureFilter::Nearest},
        error);
    if (!replacement.frameBuffer)
      return false;
    if (target.handles.frameBuffer)
      resources_.destroy(target.handles.frameBuffer);
    if (target.handles.depth)
      resources_.destroy(target.handles.depth);
    if (target.handles.color)
      resources_.destroy(target.handles.color);
    target = {replacement, light.shadowResolution};
  }
  const float radius = light.shadowDistance;
  const Vec3 right = unit(cross(
      direction, std::abs(direction.y) > .99F ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
  const Vec3 up = cross(right, direction);
  const auto forward = unit(camera.forward);
  Vec3 center{camera.position.x + forward.x * radius * .5F,
              camera.position.y + forward.y * radius * .5F,
              camera.position.z + forward.z * radius * .5F};
  const float texel = 2 * radius / light.shadowResolution;
  for (auto axis : {right, up}) {
    const float amount =
        std::round(dot(center, axis) / texel) * texel - dot(center, axis);
    center = {center.x + axis.x * amount, center.y + axis.y * amount,
              center.z + axis.z * amount};
  }
  const Vec3 eye{center.x - direction.x * radius * 2,
                 center.y - direction.y * radius * 2,
                 center.z - direction.z * radius * 2};
  values_[0] = {right.x / (2 * radius), right.y / (2 * radius),
                right.z / (2 * radius), .5F - dot(right, eye) / (2 * radius)};
  values_[1] = {up.x / (2 * radius), up.y / (2 * radius), up.z / (2 * radius),
                .5F - dot(up, eye) / (2 * radius)};
  values_[2] = {direction.x / (4 * radius), direction.y / (4 * radius),
                direction.z / (4 * radius),
                -dot(direction, eye) / (4 * radius)};
  values_[3] = {-1, light.shadowBias / (4 * radius),
                1.F / light.shadowResolution,
                caps.originBottomLeft ? 0.F : 1.F};
  frame_ = camera;
  frame_->destinationSamples = 1;
  frame_->lodPosition = camera.lodPosition.value_or(camera.position);
  frame_->camera.renderTarget.clear();
  frame_->camera.renderScale = 1;
  frame_->camera.perspective = false;
  frame_->camera.orthographicSize = radius * 2;
  frame_->camera.nearClip = radius * .001F;
  frame_->camera.farClip = radius * 4;
  frame_->camera.clearMode = "color";
  frame_->camera.clearColor = {1, 1, 1, 1};
  frame_->camera.renderHud = false;
  frame_->camera.debugMode.clear();
  frame_->position = eye;
  frame_->forward = direction;
  frame_->up = up;
  frame_->viewportX = frame_->viewportY = 0;
  frame_->viewportWidth = frame_->viewportHeight =
      std::uint16_t(light.shadowResolution);
  frame_->frameBuffer = target.handles.frameBuffer;
  frame_->postProcess.reset();
  frame_->debugGeometry = {};
  return true;
}
void DirectionalShadow3D::receive() {
  values_[3][0] = frame_ ? 1.F : 0.F;
  if (frame_) {
    const auto key = frame_->cameraId.empty() ? std::to_string(frame_->viewId)
                                              : frame_->cameraId;
    sampled_ = targets_.at(key).handles.color;
  }
}
template <class Draw>
bool DirectionalShadow3D::submitMesh(Draw draw, std::string &error) {
  drawUniforms_.assign(draw.uniforms.begin(), draw.uniforms.end());
  for (std::size_t i = 0; i < 4; ++i)
    drawUniforms_.push_back({.handle = uniforms_[i], .values = values_[i]});
  drawTextures_.assign(draw.textures.begin(), draw.textures.end());
  drawTextures_.push_back(
      {.stage = 1, .texture = sampled_, .sampler = sampler_});
  draw.uniforms = drawUniforms_;
  draw.textures = drawTextures_;
  return commands_.submit(draw, error);
}
bool DirectionalShadow3D::submit(const BufferedDraw &draw, std::string &error) {
  return submitMesh(draw, error);
}
bool DirectionalShadow3D::submit(const InstancedBufferedDraw &draw,
                                 std::string &error) {
  return submitMesh(draw, error);
}
} // namespace demi::runtime::render
