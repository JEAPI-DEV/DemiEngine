#include "demi/runtime/app/StartupPresentation.h"
#include "demi/assets/AssetGroup.h"
#include "demi/runtime/app/Bgfx2DAppHost.h"
#include "demi/runtime/app/Bgfx3DAppHost.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

namespace demi::runtime {
StartupPresentation::StartupPresentation(std::string title) {
  world_.ui.canvasSize = {1280, 720};
  ui::UiNode heading;
  heading.id = "startup_title";
  heading.type = "label";
  heading.text = std::move(title);
  heading.layout.position = {48, 300};
  heading.layout.size = {1184, 60};
  heading.fontSize = 30;
  world_.ui.nodes.push_back(std::move(heading));
  ui::UiNode status;
  status.id = "startup_progress";
  status.type = "label";
  status.text = "Loading…";
  status.layout.position = {48, 380};
  status.layout.size = {1184, 40};
  world_.ui.nodes.push_back(std::move(status));
  ui::UiLayoutEngine{}.layout(world_.ui, world_.ui.canvasSize);
}
bool StartupPresentation::renderStatus(Bgfx3DAppHost &host,
                                       std::string_view status,
                                       std::string &error) {
  world_.ui.nodes[1].text = status;
  render::BgfxCameraFrame3D camera;
  camera.camera.clearColor = {.04F, .06F, .08F, 1};
  return host.renderFrame(world_, camera, host.frameState().deltaSeconds, error);
}

void StartupPresentation::update(const assets::AssetGroupProgress &progress) {
  world_.ui.nodes[1].text =
      progress.totalAssets
          ? "Loading… " + std::to_string(progress.completedAssets) + " / " +
                std::to_string(progress.totalAssets)
          : "Loading…";
}
bool StartupPresentation::pump(Bgfx3DAppHost &host, InputState &input,
                               const assets::AssetGroupProgress &progress,
                               std::string &error) {
  host.poll(input);
  const auto &state = host.frameState();
  if (state.quitRequested) {
    return false;
  }
  if (state.minimized || state.suspended || !state.drawableAvailable ||
      !state.surfaceSettled) {
    return true;
  }
  update(progress);
  render::BgfxCameraFrame3D camera;
  camera.camera.clearColor = {.04F, .06F, .08F, 1};
  return host.renderFrame(world_, camera, state.deltaSeconds, error);
}
bool StartupPresentation::pump(Bgfx2DAppHost &host, InputState &input,
                               const assets::AssetGroupProgress &progress,
                               std::string &error) {
  host.poll(input);
  const auto &state = host.frameState();
  if (state.quitRequested) {
    return false;
  }
  if (state.minimized || state.suspended || !state.drawableAvailable ||
      !state.surfaceSettled) {
    return true;
  }
  update(progress);
  return host.renderFrame(world_, Camera2DComponent{}, {}, state.deltaSeconds,
                          nullptr, error);
}
} // namespace demi::runtime
