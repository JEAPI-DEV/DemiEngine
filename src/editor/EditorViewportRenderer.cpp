#include "editor/EditorViewportRenderer.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/SceneAssetReferences.h"
#include <set>
#include <algorithm>
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <bgfx/bgfx.h>

#include <vector>

namespace demi::editor {

EditorViewportRenderer::EditorViewportRenderer(
    const std::uint16_t firstView, runtime::render::GpuResources &resources,
    runtime::render::RenderCommands &commands)
    : resources_(resources), commands_(commands), firstView_(firstView),
      target_(std::make_unique<runtime::render::RenderTargetHandles>()) {}

EditorViewportRenderer::~EditorViewportRenderer() { release(); }

bool EditorViewportRenderer::configure(
    const std::filesystem::path &projectDirectory, std::string &error) {
  release();
  assets_ = loadAssetRegistry(projectDirectory);
  const auto &assets = assets_;
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
  return true;
}

bool EditorViewportRenderer::prepare3DAssets(std::string &error) {
  if (!assetError_.empty()) {
    error = assetError_;
    return false;
  }
  if (assetsReady_) {
    return true;
  }
  std::vector<std::string> diagnostics;
  bool success = true;
  if (!assetsPreparing_) {
    success = renderer3D_->beginAssetPreparation(assets_, assetJobs_, diagnostics);
    assetsPreparing_ = success;
  } else {
    success = renderer3D_->stepAssetPreparation(3.0, assetsReady_, diagnostics);
  }
  if (!success) {
    error = diagnostics.empty() ? "Viewport resource preparation failed" : diagnostics.front();
    assetError_ = error;
    assetsPreparing_ = false;
  }
  return success;
}

bool EditorViewportRenderer::prepare2DAssets(std::span<const std::string> roots,
                                            std::string &error) {
  if (assets2DReady_ && std::ranges::all_of(roots, [&](const auto &root) { return loaded2DRoots_.contains(root); })) {
    return true;
  }
  AssetRegistry subset;
  subset.projectDirectory = assets_.projectDirectory;
  std::set<std::string> wanted = loaded2DRoots_;
  wanted.insert(roots.begin(), roots.end());
  std::vector<std::string> pending(wanted.begin(), wanted.end());
  for (std::size_t index = 0; index < pending.size(); ++index) {
    if (const auto *asset = findAsset(assets_, pending[index])) {
      for (const auto &dependency : asset->dependencies) {
        if (wanted.insert(dependency).second) {
          pending.push_back(dependency);
        }
      }
    }
  }
  for (const auto &asset : assets_.assets) {
    if (wanted.contains(asset.id)) {
      subset.assets.push_back(asset);
    }
  }
  std::vector<std::string> diagnostics;
  assets2DReady_ = renderer2D_->loadAssets(subset, diagnostics);
  if (!assets2DReady_) {
    error = diagnostics.empty() ? "HUD resource preparation failed" : diagnostics.front();
  }
  if (assets2DReady_) {
    loaded2DRoots_ = std::move(wanted);
  }
  return assets2DReady_;
}

void EditorViewportRenderer::release() {
  assetsPreparing_ = assetsReady_ = assets2DReady_ = false;
  assetError_.clear();
  assets_ = {};
  loaded2DRoots_.clear();
  if (renderer2D_ != nullptr)
    renderer2D_->shutdown();
  if (renderer3D_ != nullptr)
    renderer3D_->shutdown();
  renderer2D_.reset();
  renderer3D_.reset();
  terrainDemand_.reset();
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
  runtime::ProfileScope scope("Editor.viewport3d");
  if (!targetReady(area, error)) {
    return error.empty();
  }
  if (!prepare3DAssets(error)) {
    return false;
  }
  if (!assetsReady_) {
    return commands_.configureView2D({.id = firstView_, .width = area.width,
        .height = area.height, .clearRgba = 0x20232bffU, .clear = true,
        .frameBuffer = target_->frameBuffer}, error);
  }
  if (area.width != 0 && area.height != 0) {
    terrainDemand_.request3D();
  }
  runtime::render::BgfxCameraFrame3D frame;
  frame.cameraId = "editor-camera";
  frame.camera = camera.projection;
  // Scene inspection uses the editor camera. HUD authoring has its own stage;
  // Game View owns the combined runtime presentation.
  frame.camera.renderHud = false;
  frame.camera.renderHudToTarget = true;
  frame.position = camera.position;
  frame.forward = camera.forward;
  frame.up = camera.up;
  frame.debugGeometry = camera.debugGeometry;
  frame.viewportWidth = area.width;
  frame.viewportHeight = area.height;
  frame.viewId = firstView_;
  frame.frameBuffer = target_->frameBuffer;
  if (camera.studioLighting) {
    runtime::render::SceneLighting3D lighting;
    lighting.direction = {-0.4F, -0.8F, -0.5F, 1.0F};
    lighting.ambient = {0.3F, 0.32F, 0.36F, 1.0F};
    frame.lightingOverride = lighting;
  }
  if (runtime::RuntimeProfiler::enabled()) {
    runtime::RuntimeProfiler::setGauge("Editor.camera.x", frame.position.x);
    runtime::RuntimeProfiler::setGauge("Editor.camera.y", frame.position.y);
    runtime::RuntimeProfiler::setGauge("Editor.camera.z", frame.position.z);
    runtime::RuntimeProfiler::setGauge("Editor.camera.forward_x", frame.forward.x);
    runtime::RuntimeProfiler::setGauge("Editor.camera.forward_y", frame.forward.y);
    runtime::RuntimeProfiler::setGauge("Editor.camera.forward_z", frame.forward.z);
  }
  const bool rendered = renderer3D_->renderFrame(world, frame, deltaSeconds, error);
  runtime::RuntimeProfiler::setGauge("Editor.viewport3d.failed", rendered ? 0 : 1);
  if (rendered) {
    const auto &statistics = renderer3D_->statistics();
    runtime::RuntimeProfiler::setGauge("Editor.viewport3d.meshes_considered",
                                      statistics.consideredMeshes);
    runtime::RuntimeProfiler::setGauge("Editor.viewport3d.meshes_visible",
                                      statistics.visibleMeshes);
    runtime::RuntimeProfiler::setGauge("Editor.viewport3d.batches",
                                      statistics.batches);
    runtime::RuntimeProfiler::setGauge("Editor.viewport3d.triangles",
                                      statistics.triangles);
  }
  return rendered;
}

bool EditorViewportRenderer::render2D(const runtime::World &world,
                                      const EditorViewportArea area,
                                      const EditorSceneView2DCamera &camera,
                                      const bool showColliders,
                                      const float deltaSeconds,
                                      std::string &error) {
  if (!assets2DReady_) {
    std::vector<std::string> roots;
    for (const auto &asset : assets_.assets) {
      roots.push_back(asset.id);
    }
    if (!prepare2DAssets(roots, error)) {
      return false;
    }
  }
  terrainDemand_.requestOtherContent();
  if (!targetReady(area, error))
    return error.empty();
  if (!renderer2D_->beginFrameRegion(
          camera.projection, camera.position, firstView_, 0, 0, area.width,
          area.height, deltaSeconds, error, 1.0F, target_->frameBuffer))
    return false;
  const bool rendered = renderer2D_->drawWorld(world, showColliders);
  const bool flushed = renderer2D_->endFrame(error);
  if (!rendered && error.empty())
    error = "Could not draw the authored 2D scene and HUD.";
  return rendered && flushed;
}

bool EditorViewportRenderer::renderHud(const runtime::ui::UiDocument &document,
                                       const EditorViewportArea area,
                                       const float deltaSeconds,
                                       std::string &error) {
  {
    std::vector<std::string> roots;
    for (const auto &node : document.nodes) {
      for (const auto *reference : {&node.font, &node.texture, &node.animation}) {
        if (reference->starts_with("asset://")) {
          roots.push_back(*reference);
        }
      }
    }
    if (!prepare2DAssets(roots, error)) {
      return false;
    }
  }
  terrainDemand_.requestOtherContent();
  if (!targetReady(area, error))
    return error.empty();
  if (!renderer2D_->beginOverlayRegion(firstView_, 0, 0, area.width,
                                       area.height, deltaSeconds, error,
                                       target_->frameBuffer))
    return false;
  if (!commands_.configureView2D({.id = firstView_,
                                  .width = area.width,
                                  .height = area.height,
                                  .clearRgba = 0x20232bffU,
                                  .clear = true,
                                  .frameBuffer = target_->frameBuffer},
                                 error))
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

std::string EditorViewportRenderer::takeTerrainDiagnostic() {
  return renderer3D_ ? renderer3D_->takeTerrainDiagnostic() : std::string{};
}

bool EditorViewportRenderer::terrainNeedsFrame() const {
  return visible_ && ((assetsPreparing_ && !assetsReady_) ||
      (terrainDemand_.active() && renderer3D_ && renderer3D_->terrainPreparationPending()));
}

void EditorViewportRenderer::maintainIdleTerrain() {
  if (visible_ && !terrainDemand_.active() && renderer3D_) {
    renderer3D_->tickInactiveTerrain();
  }
}

void EditorViewportRenderer::maintainTerrainResidency() {
  const bool active = terrainDemand_.finishFrame();
  if (visible_ && renderer3D_ && !active) {
    renderer3D_->tickInactiveTerrain();
  }
}

} // namespace demi::editor
