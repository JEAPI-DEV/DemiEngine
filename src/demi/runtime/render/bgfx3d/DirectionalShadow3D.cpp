#include "demi/runtime/render/bgfx3d/DirectionalShadow3D.h"
#include "demi/runtime/render/bgfx3d/ShadowCascadeLayout3D.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

bool DirectionalShadow3D::initialize(std::string &error) {
  if (fallback_)
    return true;
  sampler_ = resources_.createSampler("s_shadowMap", error);
  constexpr std::array names{
      "u_shadowX",      "u_shadowY",     "u_shadowZ",      "u_shadowParams",
      "u_shadowConfig", "u_shadowAtlas", "u_shadowSplits", "u_shadowCamera"};
  for (std::size_t index = 0; index < names.size(); ++index)
    uniforms_[index] = resources_.createUniform(names[index], UniformType::Vec4,
                                                index < 4 ? 4 : 1, error);
  const std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                       std::byte{255}, std::byte{255}};
  fallback_ = resources_.createTexture({.data = white,
                                        .filter = TextureFilter::Nearest,
                                        .debugName = "shadow fallback"},
                                       error);
  if (!fallback_ || !sampler_ ||
      std::ranges::any_of(uniforms_, [](auto handle) { return !handle; })) {
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
  frames_.clear();
  sampled_ = fallback_;
  receiverTexture_ = {};
  cascadeValues_ = {};
  sharedValues_ = {};
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
  frames_.clear();
  cascadeValues_ = {};
  sharedValues_ = {};
  sampled_ = fallback_;
  receiverTexture_ = {};
  if (!light.castsShadows || light.shadowBudget == 0 ||
      light.shadowDistance == 0 || !camera.updateContent)
    return true;
  const auto layout = makeShadowCascadeLayout3D(light, camera, error);
  if (!layout)
    return false;
  if (layout->count == 0)
    return true;
  const int columns = layout->count == 1 ? 1 : 2;
  const int rows = layout->count > 2 ? 2 : 1;
  const auto caps = resources_.limits();
  if (light.shadowResolution > 65535 / columns ||
      light.shadowResolution > 65535 / rows ||
      std::uint32_t(light.shadowResolution) > caps.maxTextureSize / columns ||
      std::uint32_t(light.shadowResolution) > caps.maxTextureSize / rows ||
      camera.viewId + std::uint32_t(layout->count) + 3U >= caps.maxViews) {
    error = "Directional shadow atlas or camera views exceed device limits; "
            "reduce shadow_resolution or shadow_cascades";
    return false;
  }
  if (!std::isfinite(light.shadowBias) || light.shadowBias < 0) {
    error = "Directional shadow bias must be finite and nonnegative";
    return false;
  }
  const auto key =
      camera.cameraId.empty() ? std::to_string(camera.viewId) : camera.cameraId;
  auto &target = targets_[key];
  if (target.resolution != light.shadowResolution ||
      target.cascades != layout->count) {
    const auto replacement = resources_.createRenderTarget(
        {.width = std::uint16_t(light.shadowResolution * columns),
         .height = std::uint16_t(light.shadowResolution * rows),
         .debugName = "directional shadow atlas: " + key,
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
    target = {replacement, light.shadowResolution, layout->count};
  }
  receiverTexture_ = target.handles.color;
  sharedValues_[0] = {-1, 0, float(layout->count), light.shadowBlend};
  sharedValues_[1] = {float(columns), float(rows),
                      (light.shadowFilter == "hard" ? -1.F : 1.F) /
                          light.shadowResolution,
                      caps.originBottomLeft ? 0.F : 1.F};
  sharedValues_[2] = layout->splits;
  sharedValues_[3] = layout->cameraDepth;
  for (int index = 0; index < layout->count; ++index) {
    const auto &cascade = layout->cascades[index];
    const std::array<float, 4> parameters{
        light.shadowBias / cascade.depthRange,
        index == 0 ? camera.camera.nearClip : layout->splits[index - 1],
        cascade.receiverFar, 0};
    const std::array values{cascade.x, cascade.y, cascade.z, parameters};
    for (std::size_t row = 0; row < values.size(); ++row)
      std::copy(values[row].begin(), values[row].end(),
                cascadeValues_[row].begin() + index * 4);
    auto frame = camera;
    frame.viewId = std::uint16_t(camera.viewId + index);
    frame.destinationSamples = 1;
    frame.lodPosition = camera.lodPosition.value_or(camera.position);
    frame.camera.renderTarget.clear();
    frame.camera.renderScale = 1;
    frame.camera.perspective = false;
    frame.camera.orthographicSize = cascade.radius * 2;
    frame.camera.nearClip = 0.001F;
    frame.camera.farClip = cascade.depthRange;
    frame.camera.clearMode = "color";
    frame.camera.clearColor = {1, 1, 1, 1};
    frame.camera.renderHud = false;
    frame.camera.debugMode.clear();
    frame.position = cascade.eye;
    frame.forward = cascade.direction;
    frame.up = cascade.up;
    frame.viewportX = std::uint16_t((index % columns) * light.shadowResolution);
    frame.viewportY = std::uint16_t((index / columns) * light.shadowResolution);
    frame.viewportWidth = frame.viewportHeight =
        std::uint16_t(light.shadowResolution);
    frame.frameBuffer = target.handles.frameBuffer;
    frame.postProcess.reset();
    frame.debugGeometry = {};
    frames_.push_back(std::move(frame));
  }
  return true;
}

void DirectionalShadow3D::cast(std::size_t cascade) {
  sharedValues_[0][0] = -1.F;
  sharedValues_[0][1] = float(cascade);
  sampled_ = fallback_;
}

void DirectionalShadow3D::receive() {
  sharedValues_[0][0] = frames_.empty() ? 0.F : 1.F;
  sampled_ = frames_.empty() ? fallback_ : receiverTexture_;
}

template <class Draw>
bool DirectionalShadow3D::submitMesh(Draw draw, std::string &error) {
  drawUniforms_.assign(draw.uniforms.begin(), draw.uniforms.end());
  for (std::size_t index = 0; index < 4; ++index) {
    drawUniforms_.push_back({.handle = uniforms_[index],
                             .values = cascadeValues_[index],
                             .count = 4});
    drawUniforms_.push_back(
        {.handle = uniforms_[index + 4], .values = sharedValues_[index]});
  }
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
