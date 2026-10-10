#include "demi/runtime/render/bgfx3d/SceneLightBuffer3D.h"
#include <algorithm>
#include <limits>

namespace demi::runtime::render {
bool SceneLightBuffer3D::prepare(std::uint16_t view,
                                 const SceneLighting3D &lighting,
                                 std::string &error) {
  if (!sampler_)
    sampler_ = resources_.createSampler("s_lightData", error);
  if (!parameters_)
    parameters_ =
        resources_.createUniform("u_lightData", UniformType::Vec4, 1, error);
  if (!sampler_ || !parameters_)
    return false;
  if (!fallback_) {
    constexpr std::array<std::byte, 4> black{};
    fallback_ =
        resources_.createTexture({.width = 1,
                                  .height = 1,
                                  .format = TextureFormat::RGBA8,
                                  .data = black,
                                  .filter = TextureFilter::Nearest,
                                  .debugName = "Empty scene light data"},
                                 error);
    if (!fallback_)
      return false;
  }
  if (lighting.lights.empty()) {
    if (const auto found = views_.find(view); found != views_.end())
      found->second.parameters[0] = 0;
    return true;
  }
  const auto limit =
      std::min<std::uint32_t>(resources_.limits().maxTextureSize, UINT16_MAX);
  const auto width = std::min<std::uint32_t>(256, limit) / 4 * 4;
  if (width == 0 || lighting.lights.size() > std::size_t(width / 4) * limit) {
    error = "Scene light data exceeds the GPU texture capacity.";
    return false;
  }
  const auto height = std::max<std::size_t>(
      1, (lighting.lights.size() * 4 + width - 1) / width);
  auto &target = views_[view];
  const bool resize =
      !target.texture || target.width != width || target.height != height;
  // This is the only owner of the shader's packed representation. The world
  // and scene extraction keep named light fields instead of GPU texel offsets.
  std::vector<float> data(width * height * 4, 0);
  for (std::size_t index = 0; index < lighting.lights.size(); ++index) {
    const auto &light = lighting.lights[index];
    const std::array<float, 16> record{light.position.x,
                                       light.position.y,
                                       light.position.z,
                                       light.range,
                                       light.color.r,
                                       light.color.g,
                                       light.color.b,
                                       light.intensity,
                                       light.direction.x,
                                       light.direction.y,
                                       light.direction.z,
                                       light.outerConeCos,
                                       light.innerConeCos,
                                       float(light.kind),
                                       0,
                                       0};
    std::copy(record.begin(), record.end(), data.begin() + index * 16);
  }
  if (resize || target.source != data) {
    const auto bytes = std::as_bytes(std::span(data));
    const auto upload = [&](TextureHandle texture) {
      return resources_.updateTexture(texture,
                                      {.width = std::uint16_t(width),
                                       .height = std::uint16_t(height),
                                       .data = bytes},
                                      error);
    };
    if (resize) {
      const auto texture =
          resources_.createTexture({.width = std::uint16_t(width),
                                    .height = std::uint16_t(height),
                                    .format = TextureFormat::RGBA32F,
                                    .data = {},
                                    .filter = TextureFilter::Nearest,
                                    .wrap = TextureWrap::Clamp,
                                    .debugName = "Scene light data"},
                                   error);
      if (!texture)
        return false;
      if (!upload(texture)) {
        resources_.destroy(texture);
        return false;
      }
      if (target.texture)
        resources_.destroy(target.texture);
      target.texture = texture;
      target.width = width;
      target.height = height;
    } else if (!upload(target.texture)) {
      return false;
    }
    target.source = std::move(data);
  }
  target.parameters = {float(lighting.lights.size()), float(width),
                       float(height), 0};
  return true;
}
void SceneLightBuffer3D::shutdown() {
  for (auto &[view, target] : views_)
    if (target.texture)
      resources_.destroy(target.texture);
  views_.clear();
  if (fallback_)
    resources_.destroy(fallback_);
  fallback_ = {};
  if (sampler_)
    resources_.destroy(sampler_);
  if (parameters_)
    resources_.destroy(parameters_);
  sampler_ = {};
  parameters_ = {};
}
template <class Draw>
bool SceneLightBuffer3D::submitMesh(Draw draw, std::string &error) {
  const auto found = views_.find(draw.viewId);
  const auto texture =
      found == views_.end() ? fallback_ : found->second.texture;
  const auto &parameters =
      found == views_.end() ? noLights_ : found->second.parameters;
  if (!texture)
    return commands_.submit(draw, error);
  uniforms_.assign(draw.uniforms.begin(), draw.uniforms.end());
  uniforms_.push_back({.handle = parameters_, .values = parameters});
  textures_.assign(draw.textures.begin(), draw.textures.end());
  textures_.push_back({.stage = 2, .texture = texture, .sampler = sampler_});
  draw.uniforms = uniforms_;
  draw.textures = textures_;
  return commands_.submit(draw, error);
}
bool SceneLightBuffer3D::submit(const BufferedDraw &draw, std::string &error) {
  return submitMesh(draw, error);
}
bool SceneLightBuffer3D::submit(const InstancedBufferedDraw &draw,
                                std::string &error) {
  return submitMesh(draw, error);
}
} // namespace demi::runtime::render
