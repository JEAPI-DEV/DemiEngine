#include "demi/runtime/render/bgfx3d/OpaqueSceneCapture3D.h"

namespace demi::runtime::render {
WaterSceneCapture3D
OpaqueSceneCapture3D::Target::sample(bool originBottomLeft) const {
  return {.color = color,
          .linearDepth = depth.color,
          .width = width,
          .height = height,
          .originBottomLeft = originBottomLeft};
}

OpaqueSceneCapture3D::~OpaqueSceneCapture3D() { clear(); }

void OpaqueSceneCapture3D::destroy(Target &target) {
  if (target.depth.frameBuffer) {
    resources_.destroy(target.depth.frameBuffer);
  }
  if (target.depth.depth) {
    resources_.destroy(target.depth.depth);
  }
  if (target.depth.color) {
    resources_.destroy(target.depth.color);
  }
  if (target.color) {
    resources_.destroy(target.color);
  }
  target = {};
}

void OpaqueSceneCapture3D::clear() {
  for (auto &[view, target] : targets_) {
    destroy(target);
  }
  targets_.clear();
}

void OpaqueSceneCapture3D::beginPresentation() {
  for (auto &[view, target] : targets_) {
    target.used = false;
  }
}

void OpaqueSceneCapture3D::endPresentation() {
  for (auto iterator = targets_.begin(); iterator != targets_.end();) {
    if (!iterator->second.used) {
      destroy(iterator->second);
      iterator = targets_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

OpaqueSceneCapture3D::Target *
OpaqueSceneCapture3D::prepare(std::uint16_t cameraView, std::uint16_t width,
                              std::uint16_t height, std::string &error) {
  error.clear();
  const auto limits = resources_.limits();
  if (width == 0 || height == 0 || width > limits.maxTextureSize ||
      height > limits.maxTextureSize || cameraView >= limits.maxViews) {
    error =
        "Water scene capture dimensions or camera view exceed device limits.";
    return nullptr;
  }
  if (!limits.supportsTextureBlit ||
      !resources_.textureCapabilities(TextureFormat::R32F).sampled2D) {
    error = "Water scene capture requires texture copies and sampleable R32F "
            "depth.";
    return nullptr;
  }
  auto &current = targets_[cameraView];
  current.used = true;
  if (current.width == width && current.height == height && current.color &&
      current.depth.frameBuffer && current.depth.color && current.depth.depth) {
    return &current;
  }

  Target replacement;
  replacement.depth =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::R32F,
                                     .depth = true,
                                     .debugName = "Water.opaqueDepth",
                                     .filter = TextureFilter::Nearest,
                                     .msaaSamples = 1},
                                    error);
  const bool completeDepth = replacement.depth.frameBuffer &&
                             replacement.depth.color && replacement.depth.depth;
  if (completeDepth) {
    replacement.color =
        resources_.createTexture({.width = width,
                                  .height = height,
                                  .format = TextureFormat::RGBA8,
                                  .data = {},
                                  .debugName = "Water.opaqueColor",
                                  .blitDestination = true},
                                 error);
  }
  if (!completeDepth || !replacement.color) {
    if (error.empty()) {
      error = "Water scene capture could not allocate complete color/depth "
              "targets.";
    }
    destroy(replacement);
    return nullptr;
  }
  replacement.width = width;
  replacement.height = height;
  replacement.used = true;
  destroy(current);
  current = replacement;
  return &current;
}
} // namespace demi::runtime::render
