#include "demi/runtime/render/bgfx2d/FogCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include <algorithm>
#include <bit>

namespace demi::runtime::render {
void FogCanvasRenderer::clear() {
  if (texture_) {
    resources_.destroy(texture_);
  }
  if (program_) {
    resources_.destroy(program_);
  }
  for (auto &handle : handles_) {
    if (handle) {
      resources_.destroy(handle);
    }
    handle = {};
  }
  texture_ = {};
  program_ = {};
  width_ = 0;
  height_ = 0;
  revision_ = 0;
  discoveryRevision_ = 0;
}

bool FogCanvasRenderer::upload(const FogOfWar2D &fog) {
  std::string error;
  if (!program_) {
    program_ = resources_.createBuiltinProgram(BuiltinProgram::Fog2D, error);
    const std::array<const char *, 3> names{"u_fogMap", "u_fogFocus",
                                            "u_fogData"};
    for (std::size_t index = 0; index < handles_.size(); ++index) {
      handles_[index] =
          resources_.createUniform(names[index], UniformType::Vec4, 1, error);
      if (!handles_[index]) {
        clear();
        return false;
      }
    }
    if (!program_) {
      clear();
      return false;
    }
  }
  const int requiredWidth =
      std::max(fog.width(), static_cast<int>(fog.boundary().size()));
  if (!texture_ || width_ < requiredWidth || height_ != fog.height() + 1) {
    if (texture_) {
      resources_.destroy(texture_);
    }
    width_ = static_cast<int>(
        std::bit_ceil(static_cast<unsigned>(std::max(requiredWidth, 1))));
    height_ = fog.height() + 1;
    TextureCreateInfo info;
    info.width = static_cast<std::uint16_t>(width_);
    info.height = static_cast<std::uint16_t>(height_);
    info.format = TextureFormat::RGBA32F;
    info.filter = TextureFilter::Nearest;
    info.debugName = "fog-geometry-and-discovery";
    texture_ = resources_.createTexture(info, error);
    if (!texture_) {
      return false;
    }
    revision_ = 0;
    discoveryRevision_ = 0;
  }
  // One row contains the visibility polygon. Remaining rows contain one
  // discovery value per map cell. No CPU-generated screen/mask pixels.
  if (revision_ != fog.revision() && !fog.boundary().empty()) {
    std::vector<std::array<float, 4>> data;
    data.reserve(fog.boundary().size());
    for (const auto &point : fog.boundary()) {
      data.push_back({point.angle, point.point.x, point.point.y, 0});
    }
    TextureUpdateInfo update;
    update.width = static_cast<std::uint16_t>(data.size());
    update.height = 1;
    update.data = std::as_bytes(std::span(data));
    if (!resources_.updateTexture(texture_, update, error)) {
      return false;
    }
    revision_ = fog.revision();
  }
  if (discoveryRevision_ != fog.discoveryRevision()) {
    std::vector<std::array<float, 4>> data;
    data.reserve(fog.discovered().size());
    for (auto discovered : fog.discovered()) {
      data.push_back({static_cast<float>(discovered), 0, 0, 0});
    }
    TextureUpdateInfo update;
    update.y = 1;
    update.width = static_cast<std::uint16_t>(fog.width());
    update.height = static_cast<std::uint16_t>(fog.height());
    update.data = std::as_bytes(std::span(data));
    if (!resources_.updateTexture(texture_, update, error)) {
      return false;
    }
    discoveryRevision_ = fog.discoveryRevision();
  }
  return true;
}

bool FogCanvasRenderer::draw(Canvas2D &canvas, const FogOfWar2D &fog,
                             const Camera2DComponent &camera,
                             Vec2 cameraPosition, std::uint16_t viewportWidth,
                             std::uint16_t viewportHeight) {
  if (fog.discovered().empty()) {
    clear();
    return true;
  }
  if (!upload(fog)) {
    return false;
  }
  const float ppu =
      viewportHeight / std::max(camera.orthographicSize * 2, 1.0F);
  const Vec2 extent = fog.size();
  const Vec2 origin = fog.origin();
  values_[0] = {extent.x, extent.y, static_cast<float>(fog.width()),
                static_cast<float>(fog.height())};
  values_[1] = {fog.focus().x - origin.x, fog.focus().y - origin.y,
                fog.radius(), fog.feather()};
  values_[2] = {static_cast<float>(width_), static_cast<float>(height_),
                static_cast<float>(fog.boundary().size()), fog.memoryOpacity()};
  for (std::size_t index = 0; index < uniforms_.size(); ++index) {
    uniforms_[index] = {handles_[index], values_[index], 1};
  }
  constexpr std::uint32_t fogUniformSet = 0xffff0001U;
  canvas.setUniformSet(fogUniformSet, uniforms_);
  const Rect2D rectangle{
      viewportWidth * 0.5F + (origin.x - cameraPosition.x) * ppu,
      viewportHeight * 0.5F - (origin.y + extent.y - cameraPosition.y) * ppu,
      extent.x * ppu, extent.y * ppu};
  return canvas.image(texture_, rectangle, {},
                      packVertexColorRgba8(fog.color()), BlendMode::Alpha, {},
                      program_, fogUniformSet);
}
} // namespace demi::runtime::render
