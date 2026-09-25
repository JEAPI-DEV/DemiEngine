#include "editor/EditorDockingState.h"
#include "editor/EditorGameRenderer.h"
#include "editor/EditorImGuiInput.h"
#include "editor/EditorRecoveryStore.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorInputOwnership.h"
#include "editor/EditorFontLoader.h"
#include "demi/runtime/render/backend/DefaultFont.h"
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
    const auto fontData = runtime::render::defaultFontData();
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    fontConfig.FontLoader = fontLoader_.loader();
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
        const_cast<std::byte *>(fontData.data()), static_cast<int>(fontData.size()),
        fontSize, &fontConfig);
    if (!io.FontDefault) {
      error = "Could not initialize the bundled Inter editor font.";
      shutdownGraphics();
      return false;
    }
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

  void setUiScale(const float scale) override {
    uiScale_ = std::clamp(scale, 1.0F, 2.5F);
  }

  bool beginFrame(std::string &error) override {
    platform_->poll(input_);
    const auto &frame = platform_->frameState();
    if (!frame.minimized && frame.width > 0 && frame.height > 0 &&
        !graphics_.resize(static_cast<std::uint32_t>(frame.width),
                          static_cast<std::uint32_t>(frame.height), error))
      return false;

    graphics_.beginFrame(0x111318ff);
    auto routed=inputOwnership_.route(input_,exclusiveGame_ && frame.focused);
    if(routed.changed) {
      ImGui::GetIO().ClearInputKeys();
      ImGui::GetIO().ClearInputMouse();
    }
    const auto &editorInput=routed.input;
    std::uint8_t buttons = 0;
    if (editorInput.mouseButtonsDown.contains("left"))
      buttons |= IMGUI_MBUT_LEFT;
    if (editorInput.mouseButtonsDown.contains("right"))
      buttons |= IMGUI_MBUT_RIGHT;
    if (editorInput.mouseButtonsDown.contains("middle"))
      buttons |= IMGUI_MBUT_MIDDLE;
    // The bgfx sample wrapper's scroll parameter is a cumulative integer.
    // Demi's platform input is a per-frame float delta, so submit it through
    // ImGui's native queue and keep the legacy wrapper channel fixed at zero.
    submitEditorImGuiInput(editorInput);
    ImGui::GetIO().AddFocusEvent(frame.focused);
    ImGui::GetIO().DisplayFramebufferScale = {uiScale_, uiScale_};
    imguiBeginFrame(
        routed.exclusive ? INT32_MIN : static_cast<std::int32_t>(editorInput.mousePosition.x / uiScale_),
        routed.exclusive ? INT32_MIN : static_cast<std::int32_t>(editorInput.mousePosition.y / uiScale_), buttons,
        0, static_cast<std::uint16_t>(std::clamp(width(), 1, 65535)),
        static_cast<std::uint16_t>(std::clamp(height(), 1, 65535)), -1,
        ImGuiViewId);
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
    return viewportRenderer_->prepareTarget(framebufferArea(area), error);
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
    return viewportRenderer_->render3D(world, framebufferArea(area), camera,
                                       platform_->frameState().deltaSeconds,
                                       error);
  }

  bool renderViewport2D(const runtime::World &world,
                        const EditorViewportArea area,
                        const EditorSceneView2DCamera &camera,
                        const bool showColliders, std::string &error) override {
    const auto &frame = platform_->frameState();
    if (frame.minimized || frame.width <= 0 || frame.height <= 0 ||
        area.width == 0 || area.height == 0)
      return true;
    return viewportRenderer_->render2D(world, framebufferArea(area), camera,
                                       showColliders, frame.deltaSeconds,
                                       error);
  }

  bool renderHud(const runtime::ui::UiDocument &document,
                 const EditorViewportArea area, std::string &error) override {
    const auto &frame = platform_->frameState();
    if (frame.minimized || frame.width <= 0 || frame.height <= 0 ||
        area.width == 0 || area.height == 0)
      return true;
    return viewportRenderer_->renderHud(document, framebufferArea(area),
                                        frame.deltaSeconds, error);
  }

  bool configureGameRenderer(const std::filesystem::path &projectDirectory,
                             std::string &error) override {
    return gameRenderer_->configure(projectDirectory, error);
  }

  void releaseGameRenderer() override { gameRenderer_->release(); }

  bool prepareGameTarget(const EditorViewportArea area,
                         std::string &error) override {
    return gameRenderer_->prepareTarget(framebufferArea(area), error);
  }

  bool renderGame(const runtime::World &world, const EditorViewportArea area,
                  const float interpolationAlpha, std::string &error) override {
    return gameRenderer_->render(world, framebufferArea(area), deltaSeconds(),
                                 interpolationAlpha, error);
  }

  runtime::InputState gameInput(const EditorViewportArea area,
                                const bool focused) const override {
    if (!focused || !platform_->frameState().focused || (exclusiveGame_ && !mouseCaptured_))
      return {};
    runtime::InputState result = input_;
    result.mousePosition.x = result.mousePosition.x / uiScale_ - area.x;
    result.mousePosition.y = result.mousePosition.y / uiScale_ - area.y;
    if(exclusiveGame_ && mouseCaptured_) result.mousePosition={area.width*.5F,area.height*.5F};
    result.mouseDelta.x /= uiScale_;
    result.mouseDelta.y /= uiScale_;
    return result;
  }

  std::uint16_t gameTextureIndex() const override {
    return gameRenderer_->textureIndex();
  }
  bool gamePointerInside(EditorViewportArea area) const override {
    if(!platform_->frameState().focused || !area.width || !area.height) return false;
    if(exclusiveGame_ && mouseCaptured_) return true;
    const auto x=input_.mousePosition.x/uiScale_-area.x;
    const auto y=input_.mousePosition.y/uiScale_-area.y;
    return x>=0 && y>=0 && x<area.width && y<area.height;
  }

  float deltaSeconds() const override {
    return platform_ == nullptr ? 0.0F : platform_->frameState().deltaSeconds;
  }

  bool setViewportInputCaptured(const bool captured,
                                std::string &error,bool exclusiveGame,
                                bool cursorVisible) override {
    const bool effective=captured && platform_->frameState().focused;
    if (effective != mouseCaptured_ && !platform_->setMouseCaptured(effective, error)) {
      // Keep GUI input quarantined if native capture failed; Ctrl+D can recover.
      if(effective && exclusiveGame) exclusiveGame_=true;
      return false;
    }
    mouseCaptured_ = effective;
    exclusiveGame_ = exclusiveGame && effective;
    return platform_->setMouseVisible(!effective && (!platform_->frameState().focused || cursorVisible),error);
  }

  void endFrame() override {
    imguiEndFrame();
    // The bgfx sample adapter scales scissors for framebuffer density but
    // still uses logical DisplaySize for its view rectangle. Match the
    // physical swapchain here without modifying the pinned dependency.
    const auto &frame = platform_->frameState();
    bgfx::setViewRect(
        ImGuiViewId, 0, 0,
        static_cast<std::uint16_t>(std::clamp(frame.width, 1, 65535)),
        static_cast<std::uint16_t>(std::clamp(frame.height, 1, 65535)));
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

  int width() const override {
    return static_cast<int>(platform_->frameState().width / uiScale_);
  }
  int height() const override {
    return static_cast<int>(platform_->frameState().height / uiScale_);
  }
  std::string rendererName() const override {
    return std::string(graphics_.rendererName());
  }

private:
  static constexpr bgfx::ViewId ImGuiViewId = 255;
  EditorViewportArea framebufferArea(const EditorViewportArea area) const {
    const auto scaled = [this](std::uint16_t value) {
      return static_cast<std::uint16_t>(
          std::clamp(value * uiScale_, 0.0F, 65535.0F));
    };
    return {.x = scaled(area.x),
            .y = scaled(area.y),
            .width = scaled(area.width),
            .height = scaled(area.height)};
  }
  float uiScale_ = 1.0F;
  EditorFontLoader fontLoader_{runtime::render::defaultFontVariations()};
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
  bool exclusiveGame_ = false;
  EditorInputOwnership inputOwnership_;
};

} // namespace

std::unique_ptr<EditorUiHost> createEditorUiHost() {
  return std::make_unique<BgfxEditorUiHost>();
}

} // namespace demi::editor
