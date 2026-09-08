#include "editor/EditorDockingState.h"
#include "editor/EditorGameRenderer.h"
#include "editor/EditorImGuiInput.h"
#include "editor/EditorRecoveryStore.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorViewportRenderer.h"
#include "editor/EditorWorkspaceLayout.h"

#include "demi/runtime/platform/PlatformHost.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/ImageDecoder2D.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace demi::editor {
namespace {

using demi::runtime::InputState;
using demi::runtime::platform::PlatformHost;
using demi::runtime::render::BgfxGraphicsDevice;

bool readBytes(const std::filesystem::path &path, std::vector<std::byte> &bytes,
               std::string &error) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    error = "Could not open editor branding image: " + path.string();
    return false;
  }
  const std::streamsize size = input.tellg();
  if (size <= 0) {
    error = "Editor branding image is empty.";
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(bytes.data()), size)) {
    error = "Could not read the editor branding image.";
    return false;
  }
  return true;
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

    const float fontSize = editorFontSize(frame.logicalDpi);
    imguiCreate(fontSize);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const EditorLayoutPreparation layout = dockingState_.prepareLayout();
    workspaceDiagnostic_ = layout.diagnostic;
    std::error_code directoryError;
    std::filesystem::create_directories(
        dockingState_.layoutPath().parent_path(), directoryError);
    if (directoryError) {
      workspaceDiagnostic_ = "Workspace layout persistence is unavailable: " +
                             directoryError.message();
      io.IniFilename = nullptr;
    } else {
      imguiIniPath_ = dockingState_.layoutPath().string();
      io.IniFilename = imguiIniPath_.c_str();
    }
    resources_ = demi::runtime::render::createBgfxGpuResources();
    commands_ = demi::runtime::render::createBgfxRenderCommands(*resources_);
    if (commands_ == nullptr) {
      error = "Could not create editor viewport render commands.";
      shutdownGraphics();
      return false;
    }
    viewportRenderer_ =
        std::make_unique<EditorViewportRenderer>(*resources_, *commands_);
    gameRenderer_ =
        std::make_unique<EditorGameRenderer>(*resources_, *commands_);
    initialized_ = true;
    return true;
  }

  bool loadBranding(std::string &error) override {
    if (brandingTexture_)
      return true;
    std::vector<std::byte> encoded;
    if (!readBytes(DEMI_EDITOR_BRANDING_PATH, encoded, error))
      return false;
    runtime::render::ImageData2D image;
    if (!runtime::render::decodeImage2D(encoded, image, error))
      return false;
    brandingTexture_ = resources_->createTexture(
        {.width = image.width,
         .height = image.height,
         .format = runtime::render::TextureFormat::RGBA8,
         .data = image.rgba,
         .filter = runtime::render::TextureFilter::Linear,
         .wrap = runtime::render::TextureWrap::Clamp,
         .debugName = "DemiEngine editor branding"},
        error);
    return static_cast<bool>(brandingTexture_);
  }

  std::uint16_t brandingTextureIndex() const override {
    const auto *lookup =
        dynamic_cast<const runtime::render::BgfxResourceLookup *>(
            resources_.get());
    if (lookup == nullptr || !brandingTexture_)
      return UINT16_MAX;
    return lookup->bgfxTexture(brandingTexture_).idx;
  }

  EditorGpuTimingSample gpuTimingSample() const override {
    return gpuTimingSample_;
  }

  void shutdown() override {
    if (!initialized_)
      return;
    releaseGameRenderer();
    gameRenderer_.reset();
    viewportRenderer_->release();
    viewportRenderer_.reset();
    commands_.reset();
    resources_->clear();
    brandingTexture_ = {};
    resources_.reset();
    if (ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().IniFilename != nullptr)
      ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
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
        static_cast<std::int32_t>(input_.mousePosition.y), buttons, 0,
        static_cast<std::uint16_t>(std::clamp(frame.width, 1, 65535)),
        static_cast<std::uint16_t>(std::clamp(frame.height, 1, 65535)));
    return true;
  }

  std::vector<std::filesystem::path> takeDroppedFiles() override {
    return platform_->takeDroppedFiles();
  }

  std::string takeWorkspaceDiagnostic() override {
    return std::exchange(workspaceDiagnostic_, {});
  }

  bool configureViewport(const std::filesystem::path &projectDirectory,
                         std::string &error) override {
    return viewportRenderer_->configure(projectDirectory, error);
  }

  bool prepareViewportTarget(const EditorViewportArea area,
                             std::string &error) override {
    return viewportRenderer_->prepareTarget(area, error);
  }

  std::uint16_t viewportTextureIndex() const override {
    return viewportRenderer_->textureIndex();
  }

  bool renderViewport(const runtime::World &world,
                      const EditorViewportArea area,
                      const EditorSceneViewCamera &camera,
                      std::string &error) override {
    const auto &platformFrame = platform_->frameState();
    if (platformFrame.minimized || platformFrame.width <= 0 ||
        platformFrame.height <= 0 || area.width == 0 || area.height == 0)
      return true;
    return viewportRenderer_->render3D(
        world, area, camera, platform_->frameState().deltaSeconds, error);
  }

  bool renderViewport2D(const runtime::World &world,
                        const EditorViewportArea area,
                        const EditorSceneView2DCamera &camera,
                        const bool showColliders, std::string &error) override {
    const auto &frame = platform_->frameState();
    if (frame.minimized || frame.width <= 0 || frame.height <= 0 ||
        area.width == 0 || area.height == 0)
      return true;
    return viewportRenderer_->render2D(world, area, camera, showColliders,
                                       frame.deltaSeconds, error);
  }

  bool renderHud(const runtime::ui::UiDocument &document,
                 const EditorViewportArea area, std::string &error) override {
    const auto &frame = platform_->frameState();
    if (frame.minimized || frame.width <= 0 || frame.height <= 0 ||
        area.width == 0 || area.height == 0)
      return true;
    return viewportRenderer_->renderHud(document, area, frame.deltaSeconds,
                                        error);
  }

  bool configureGameRenderer(const std::filesystem::path &projectDirectory,
                             std::string &error) override {
    return gameRenderer_->configure(projectDirectory, error);
  }

  void releaseGameRenderer() override { gameRenderer_->release(); }

  bool prepareGameTarget(const EditorViewportArea area,
                         std::string &error) override {
    return gameRenderer_->prepareTarget(area, error);
  }

  bool renderGame(const runtime::World &world, const EditorViewportArea area,
                  const float interpolationAlpha, std::string &error) override {
    return gameRenderer_->render(world, area, deltaSeconds(),
                                 interpolationAlpha, error);
  }

  runtime::InputState gameInput(const EditorViewportArea area,
                                const bool focused) const override {
    if (!focused)
      return {};
    runtime::InputState result = input_;
    result.mousePosition.x -= static_cast<float>(area.x);
    result.mousePosition.y -= static_cast<float>(area.y);
    return result;
  }

  std::uint16_t gameTextureIndex() const override {
    return gameRenderer_->textureIndex();
  }

  float deltaSeconds() const override {
    return platform_ == nullptr ? 0.0F : platform_->frameState().deltaSeconds;
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
    const bgfx::Stats *stats = bgfx::getStats();
    std::vector<EditorGpuViewCounters> views;
    if (stats != nullptr && stats->viewStats != nullptr) {
      views.reserve(stats->numViews);
      for (std::uint16_t index = 0; index < stats->numViews; ++index) {
        const bgfx::ViewStats &view = stats->viewStats[index];
        views.push_back({.viewId = view.view,
                         .name = view.name,
                         .begin = view.gpuTimeBegin,
                         .end = view.gpuTimeEnd});
      }
    }
    gpuTimingSample_ = buildEditorGpuTimingSample(
        stats != nullptr ? stats->gpuTimerFreq : 0, views);
  }

  bool shouldClose() const override {
    return platform_ == nullptr || platform_->frameState().quitRequested;
  }

  void acknowledgeCloseRequest() override {
    if (platform_ != nullptr)
      platform_->clearQuitRequest();
  }

  int width() const override { return platform_->frameState().width; }
  int height() const override { return platform_->frameState().height; }
  std::string rendererName() const override {
    return std::string(graphics_.rendererName());
  }

private:
  void shutdownGraphics() {
    viewportRenderer_.reset();
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
  EditorDockingStateStore dockingState_{defaultEditorDataDirectory()};
  BgfxGraphicsDevice graphics_;
  std::unique_ptr<demi::runtime::render::GpuResources> resources_;
  std::unique_ptr<demi::runtime::render::RenderCommands> commands_;
  std::unique_ptr<EditorViewportRenderer> viewportRenderer_;
  std::unique_ptr<EditorGameRenderer> gameRenderer_;
  runtime::render::TextureHandle brandingTexture_;
  EditorGpuTimingSample gpuTimingSample_;
  InputState input_;
  std::string imguiIniPath_;
  std::string workspaceDiagnostic_;
  bool initialized_ = false;
  bool mouseCaptured_ = false;
};

} // namespace

std::unique_ptr<EditorUiHost> createEditorUiHost() {
  return std::make_unique<BgfxEditorUiHost>();
}

} // namespace demi::editor
