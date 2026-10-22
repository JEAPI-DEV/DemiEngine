#include "editor/EditorGameRenderer.h"
#include "editor/EditorRenderViews.h"

#include "demi/runtime/assets/RegistryAssetResourceLoader.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <utility>
#include <vector>

namespace demi::editor {
namespace {

std::string preparationError(const std::vector<std::string> &diagnostics) {
  return diagnostics.empty() ? "Game view asset preparation failed."
                             : diagnostics.front();
}

} // namespace

runtime::EmbeddedRuntimeHostServices EditorGameRenderer::hostServices() {
  return {
      .createAssetLoader = [this](const AssetRegistry &registry,
                                  std::string &error)
          -> std::shared_ptr<assets::AssetResourceLoader> {
        if (!configure(registry.projectDirectory, error)) {
          return {};
        }
        return std::make_shared<runtime::RegistryAssetResourceLoader>(
            registry,
            runtime::RegistryAssetBackend{
                .name = "embedded-bgfx",
                .assetTypes = {"Font2D", "GifAnimation2D", "Icon2D",
                               "ImageAnimation2D", "Material", "Model3D",
                               "RenderTarget", "Shader", "SvgTexture2D",
                               "Texture2D", "Tilemap2D"},
                .apply =
                    [this](const AssetRegistry &assets, std::string &error) {
                      return replaceAssets(assets, error);
                    },
                .applyStep =
                    [this](const AssetRegistry &assets, double budget,
                           std::string &error) {
                      bool complete = false;
                      if (!publishAssets(assets, budget, complete, error)) {
                        return runtime::AssetPublicationStage::Failed;
                      }
                      return complete ? runtime::AssetPublicationStage::Ready
                                      : runtime::AssetPublicationStage::Pending;
                    },
                .cancelApply = [this] { cancelAssetPublication(); },
                .applyProgress =
                    [this] {
                      return preparingRenderer3D_
                                 ? preparingRenderer3D_
                                       ->assetPreparationProgress()
                                 : 0.0;
                    }});
      },
      .prepareScenePresentation =
          [this](const runtime::World &world, std::uint16_t width,
                 std::uint16_t height, bool &complete, std::string &error) {
            return prepareScenePresentation(world, width, height, complete,
                                            error);
          },
      .cancelScenePresentation = [this] { cancelScenePreparation(); }};
}

bool EditorGameRenderer::publishAssets(const AssetRegistry &registry,
                                       double budget, bool &complete,
                                       std::string &error) {
  complete = false;
  std::vector<std::string> diagnostics;
  if (!preparingRenderer3D_) {
    auto candidate = std::make_unique<runtime::render::BgfxRenderer3D>(
        resources_, commands_);
    if (!candidate->initialize(error) ||
        !candidate->beginAssetPreparation(registry, assetPreparationJobs_,
                                          diagnostics)) {
      if (error.empty()) {
        error = preparationError(diagnostics);
      }
      return false;
    }
    preparingRenderer3D_ = std::move(candidate);
    return true;
  }
  if (!preparingRenderer3D_->stepAssetPreparation(budget, complete,
                                                  diagnostics)) {
    error = preparationError(diagnostics);
    cancelAssetPublication();
    return false;
  }
  if (!complete) {
    return true;
  }
  auto candidate2D =
      std::make_unique<runtime::render::BgfxRenderer2D>(resources_, commands_);
  if (!candidate2D->initialize(error) ||
      !candidate2D->loadAssets(registry, diagnostics)) {
    if (error.empty()) {
      error = preparationError(diagnostics);
    }
    cancelAssetPublication();
    complete = false;
    return false;
  }
  // Keep the live loading HUD and its resources until both candidates are
  // ready.
  finishPresentation();
  renderer3D_ = std::move(preparingRenderer3D_);
  renderer2D_ = std::move(candidate2D);
  return true;
}

bool EditorGameRenderer::replaceAssets(const AssetRegistry &registry,
                                       std::string &error) {
  cancelAssetPublication();
  auto candidate3D =
      std::make_unique<runtime::render::BgfxRenderer3D>(resources_, commands_);
  auto candidate2D =
      std::make_unique<runtime::render::BgfxRenderer2D>(resources_, commands_);
  std::vector<std::string> diagnostics;
  if (!candidate3D->initialize(error) || !candidate2D->initialize(error) ||
      !candidate3D->loadAssets(registry, diagnostics) ||
      !candidate2D->loadAssets(registry, diagnostics)) {
    if (error.empty()) {
      error = preparationError(diagnostics);
    }
    return false;
  }
  finishPresentation();
  renderer3D_ = std::move(candidate3D);
  renderer2D_ = std::move(candidate2D);
  return true;
}

void EditorGameRenderer::cancelAssetPublication() {
  if (preparingRenderer3D_) {
    preparingRenderer3D_->cancelAssetPreparation();
    preparingRenderer3D_.reset();
  }
}

bool EditorGameRenderer::prepareScenePresentation(const runtime::World &world,
                                                  std::uint16_t width,
                                                  std::uint16_t height,
                                                  bool &complete,
                                                  std::string &error) {
  if (!renderer3D_) {
    complete = false;
    error = "Game view presentation renderer is not configured.";
    return false;
  }
  runtime::render::BgfxCameraFrame3D frame;
  frame.cameraId = "embedded-game-camera";
  frame.viewId = EditorGameFirstView;
  frame.viewportWidth = width ? width : 960;
  frame.viewportHeight = height ? height : 540;
  const auto cameras = runtime::renderCameras3D(world);
  if (!cameras.empty()) {
    const auto &entity = *cameras.front();
    frame.camera = *entity.component<runtime::Camera3DComponent>();
    if (const auto transform =
            runtime::resolveWorldTransform3D(world, entity)) {
      frame.position = transform->position;
      frame.forward =
          runtime::transformDirection3D(*transform, frame.camera.targetOffset);
      frame.up = runtime::transformDirection3D(*transform,
                                               {0, frame.camera.upAxis, 0});
    }
  }
  terrainDemand_.request3D();
  return renderer3D_->prepareScenePresentation(world, frame, complete, error);
}

void EditorGameRenderer::cancelScenePreparation() {
  if (renderer3D_) {
    renderer3D_->cancelScenePresentationPreparation();
  }
}

void EditorGameRenderer::finishPresentation() {
  if (renderer3D_) {
    renderer3D_->endPresentation(true);
  }
}

} // namespace demi::editor
