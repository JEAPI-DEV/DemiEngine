#include "editor/EditorUiHost.h"
#include "editor/EditorImGuiInput.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/platform/PlatformHost.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace demi::editor {
namespace {

using demi::runtime::InputState;
using demi::runtime::platform::PlatformHost;
using demi::runtime::render::BgfxGraphicsDevice;

runtime::render::BgfxCameraFrame3D
editorCamera(const EditorSceneViewCamera &camera,
             const EditorViewportArea area) {
  runtime::render::BgfxCameraFrame3D frame;
  frame.cameraId = "editor-camera";
  frame.camera = camera.projection;
  frame.position = camera.position;
  frame.forward = camera.forward;
  frame.up = camera.up;
  frame.debugGeometry = camera.debugGeometry;
  frame.viewportX = area.x;
  frame.viewportY = area.y;
  frame.viewportWidth = area.width;
  frame.viewportHeight = area.height;
  frame.viewId = 1;

  return frame;
}

class BgfxEditorUiHost final : public EditorUiHost {
public:
  ~BgfxEditorUiHost() override { shutdown(); }

  bool initialize(std::string title, std::string &error) override {
    platform_ = demi::runtime::platform::createSdlPlatformHost();
    if (!platform_->initialize({.title = std::move(title),
                                .width = 1680,
                                .height = 945,
                                .resizable = true},
                               error))
      return false;

    const auto &frame = platform_->frameState();
    if (!graphics_.initialize(
            {.api = demi::runtime::render::GraphicsApi::Automatic,
             .nativeWindow = platform_->nativeWindow(),
             .width = static_cast<std::uint32_t>(frame.width),
             .height = static_cast<std::uint32_t>(frame.height),
             .vsync = true,
             .debug = false},
            error)) {
      platform_->shutdown();
      return false;
    }

    const float fontSize =
        std::clamp(15.0F * frame.logicalDpi / 96.0F, 14.0F, 22.0F);
    imguiCreate(fontSize);
    ImGui::GetIO().IniFilename = nullptr;
    resources_ = demi::runtime::render::createBgfxGpuResources();
    commands_ = demi::runtime::render::createBgfxRenderCommands(*resources_);
    if (commands_ == nullptr) {
      error = "Could not create editor viewport render commands.";
      shutdownGraphics();
      return false;
    }
    renderer_ = std::make_unique<demi::runtime::render::BgfxRenderer3D>(
        *resources_, *commands_);
    if (!renderer_->initialize(error)) {
      shutdownGraphics();
      return false;
    }
    initialized_ = true;
    return true;
  }

  void shutdown() override {
    if (!initialized_)
      return;
    renderer_->shutdown();
    renderer_.reset();
    commands_.reset();
    resources_->clear();
    resources_.reset();
    imguiDestroy();
    graphics_.shutdown();
    platform_->shutdown();
    platform_.reset();
    initialized_ = false;
  }

  bool beginFrame(std::string &error) override {
    platform_->poll(input_);
    const auto &frame = platform_->frameState();
    if (!frame.minimized && frame.width > 0 && frame.height > 0 &&
        !graphics_.resize(static_cast<std::uint32_t>(frame.width),
                          static_cast<std::uint32_t>(frame.height), error))
      return false;

    graphics_.beginFrame(0x111318ff);
    std::uint8_t buttons = 0;
    if (input_.mouseButtonsDown.contains("left"))
      buttons |= IMGUI_MBUT_LEFT;
    if (input_.mouseButtonsDown.contains("right"))
      buttons |= IMGUI_MBUT_RIGHT;
    if (input_.mouseButtonsDown.contains("middle"))
      buttons |= IMGUI_MBUT_MIDDLE;
    // The bgfx sample wrapper's scroll parameter is a cumulative integer.
    // Demi's platform input is a per-frame float delta, so submit it through
    // ImGui's native queue and keep the legacy wrapper channel fixed at zero.
    submitEditorImGuiInput(input_);
    imguiBeginFrame(
        static_cast<std::int32_t>(input_.mousePosition.x),
        static_cast<std::int32_t>(input_.mousePosition.y), buttons,
        0,
        static_cast<std::uint16_t>(std::clamp(frame.width, 1, 65535)),
        static_cast<std::uint16_t>(std::clamp(frame.height, 1, 65535)));
    return true;
  }

  bool configureViewport(const std::filesystem::path &projectDirectory,
                         std::string &error) override {
    const AssetRegistry assets = loadAssetRegistry(projectDirectory);
    if (hasErrors(assets.diagnostics)) {
      error = assets.diagnostics.front().message;
      return false;
    }
    std::vector<std::string> diagnostics;
    if (!renderer_->loadAssets(assets, diagnostics)) {
      error = diagnostics.empty() ? "Could not load viewport assets."
                                  : diagnostics.front();
      return false;
    }
    return true;
  }

  bool renderViewport(const runtime::World &world,
                      const EditorViewportArea area,
                      const EditorSceneViewCamera &camera,
                      std::string &error) override {
    const auto &platformFrame = platform_->frameState();
    if (platformFrame.minimized || platformFrame.width <= 0 ||
        platformFrame.height <= 0 || area.width == 0 || area.height == 0)
      return true;
    return renderer_->renderFrame(world, editorCamera(camera, area),
                                  platform_->frameState().deltaSeconds, error);
  }

  bool setViewportInputCaptured(const bool captured,
                                std::string &error) override {
    if (captured == mouseCaptured_)
      return true;
    if (!platform_->setMouseCaptured(captured, error))
      return false;
    mouseCaptured_ = captured;
    return true;
  }

  void endFrame() override {
    imguiEndFrame();
    (void)graphics_.endFrame();
  }

  bool shouldClose() const override {
    return platform_ == nullptr || platform_->frameState().quitRequested;
  }

  int width() const override { return platform_->frameState().width; }
  int height() const override { return platform_->frameState().height; }
  std::string rendererName() const override {
    return std::string(graphics_.rendererName());
  }

private:
  void shutdownGraphics() {
    renderer_.reset();
    commands_.reset();
    if (resources_ != nullptr)
      resources_->clear();
    resources_.reset();
    imguiDestroy();
    graphics_.shutdown();
    platform_->shutdown();
    platform_.reset();
  }

  std::unique_ptr<PlatformHost> platform_;
  BgfxGraphicsDevice graphics_;
  std::unique_ptr<demi::runtime::render::GpuResources> resources_;
  std::unique_ptr<demi::runtime::render::RenderCommands> commands_;
  std::unique_ptr<demi::runtime::render::BgfxRenderer3D> renderer_;
  InputState input_;
  bool initialized_ = false;
  bool mouseCaptured_ = false;
};

} // namespace

std::unique_ptr<EditorUiHost> createEditorUiHost() {
  return std::make_unique<BgfxEditorUiHost>();
}

} // namespace demi::editor
