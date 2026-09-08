#include "editor/EditorViewportRenderer.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <bgfx/bgfx.h>

#include <vector>

namespace demi::editor {

EditorViewportRenderer::EditorViewportRenderer(
    runtime::render::GpuResources &resources,
    runtime::render::RenderCommands &commands)
    : resources_(resources), commands_(commands),
      target_(std::make_unique<runtime::render::RenderTargetHandles>()) {}

EditorViewportRenderer::~EditorViewportRenderer() { release(); }

bool EditorViewportRenderer::configure(
    const std::filesystem::path &projectDirectory, std::string &error) {
  release();
  const AssetRegistry assets = loadAssetRegistry(projectDirectory);
  if (hasErrors(assets.diagnostics)) {
    error = assets.diagnostics.front().message;
    return false;
  }
  renderer3D_ =
      std::make_unique<runtime::render::BgfxRenderer3D>(resources_, commands_);
  renderer2D_ =
      std::make_unique<runtime::render::BgfxRenderer2D>(resources_, commands_);
  if (!renderer3D_->initialize(error) || !renderer2D_->initialize(error)) {
    release();
    return false;
  }
  std::vector<std::string> diagnostics;
  if (!renderer3D_->loadAssets(assets, diagnostics) ||
      !renderer2D_->loadAssets(assets, diagnostics)) {
    error = diagnostics.empty() ? "Could not load viewport assets."
                                : diagnostics.front();
    release();
    return false;
  }
  return true;
}

void EditorViewportRenderer::release() {
  if (renderer2D_ != nullptr)
    renderer2D_->shutdown();
  if (renderer3D_ != nullptr)
    renderer3D_->shutdown();
  renderer2D_.reset();
  renderer3D_.reset();
  destroyTarget();
}

bool EditorViewportRenderer::prepareTarget(const EditorViewportArea area,
                                           std::string &error) {
  if (area.width == 0 || area.height == 0)
    return true;
  return ensureTarget(area.width, area.height, error);
}

bool EditorViewportRenderer::render3D(const runtime::World &world,
                                      const EditorViewportArea area,
                                      const EditorSceneViewCamera &camera,
                                      const float deltaSeconds,
                                      std::string &error) {
  if (!targetReady(area, error))
    return error.empty();
  runtime::render::BgfxCameraFrame3D frame;
  frame.cameraId = "editor-camera";
  frame.camera = camera.projection;
  // The editor scene is rendered into a texture rather than directly to the
  // swapchain. Keep the authored HUD in that same texture so the docked scene
  // viewport matches the runtime composition and HUD picking coordinates.
  frame.camera.renderHud = true;
  frame.camera.renderHudToTarget = true;
  frame.position = camera.position;
  frame.forward = camera.forward;
  frame.up = camera.up;
  frame.debugGeometry = camera.debugGeometry;
  frame.viewportWidth = area.width;
  frame.viewportHeight = area.height;
  frame.viewId = 1;
  frame.frameBuffer = target_->frameBuffer;
  return renderer3D_->renderFrame(world, frame, deltaSeconds, error);
}

bool EditorViewportRenderer::render2D(const runtime::World &world,
                                      const EditorViewportArea area,
                                      const EditorSceneView2DCamera &camera,
                                      const bool showColliders,
                                      const float deltaSeconds,
                                      std::string &error) {
  if (!targetReady(area, error))
    return error.empty();
  if (!renderer2D_->beginFrameRegion(camera.projection, camera.position, 1, 0,
                                     0, area.width, area.height, deltaSeconds,
                                     error, 1.0F, target_->frameBuffer))
    return false;
  const bool rendered = renderer2D_->drawWorld(world, showColliders) &&
                        renderer2D_->drawHud(world);
  const bool flushed = renderer2D_->endFrame(error);
  if (!rendered && error.empty())
    error = "Could not draw the authored 2D scene and HUD.";
  return rendered && flushed;
}

bool EditorViewportRenderer::renderHud(const runtime::ui::UiDocument &document,
                                       const EditorViewportArea area,
                                       const float deltaSeconds,
                                       std::string &error) {
  if (!targetReady(area, error))
    return error.empty();
  if (!renderer2D_->beginOverlayRegion(1, 0, 0, area.width, area.height,
                                       deltaSeconds, error,
                                       target_->frameBuffer))
    return false;
  const bool rendered = renderer2D_->drawUi(document);
  const bool flushed = renderer2D_->endFrame(error);
  if (!rendered && error.empty())
    error = "Could not draw the authored HUD.";
  return rendered && flushed;
}

std::uint16_t EditorViewportRenderer::textureIndex() const {
  if (!target_->color)
    return UINT16_MAX;
  const auto *lookup =
      dynamic_cast<const runtime::render::BgfxResourceLookup *>(&resources_);
  if (lookup == nullptr)
    return UINT16_MAX;
  const bgfx::TextureHandle texture = lookup->bgfxTexture(target_->color);
  return bgfx::isValid(texture) ? texture.idx : UINT16_MAX;
}

bool EditorViewportRenderer::targetReady(const EditorViewportArea area,
                                         std::string &error) {
  if (renderer2D_ == nullptr || renderer3D_ == nullptr) {
    error = "The authored viewport renderer is not configured.";
    return false;
  }
  if (area.width == 0 || area.height == 0)
    return false;
  if (!target_->frameBuffer)
    return ensureTarget(area.width, area.height, error);
  // The UI command list may still reference the previous texture. The target
  // is resized safely by prepareTarget before the following UI frame.
  return targetWidth_ == area.width && targetHeight_ == area.height;
}

bool EditorViewportRenderer::ensureTarget(const std::uint16_t width,
                                          const std::uint16_t height,
                                          std::string &error) {
  if (target_->frameBuffer && targetWidth_ == width && targetHeight_ == height)
    return true;
  destroyTarget();
  *target_ = resources_.createRenderTarget(
      {.width = width,
       .height = height,
       .colorFormat = runtime::render::TextureFormat::RGBA8,
       .depth = true,
       .debugName = "Editor authored viewport"},
      error);
  if (!target_->frameBuffer || !target_->color) {
    destroyTarget();
    return false;
  }
  targetWidth_ = width;
  targetHeight_ = height;
  return true;
}

void EditorViewportRenderer::destroyTarget() {
  if (target_->frameBuffer)
    resources_.destroy(target_->frameBuffer);
  if (target_->depth)
    resources_.destroy(target_->depth);
  if (target_->color)
    resources_.destroy(target_->color);
  *target_ = {};
  targetWidth_ = 0;
  targetHeight_ = 0;
}

} // namespace demi::editor
