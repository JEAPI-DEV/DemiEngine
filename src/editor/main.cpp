#include "cli/CommandLine.h"
#include "editor/EditorShell.h"
#include "editor/EditorTheme.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectDiscovery.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <fstream>

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct EditorOptions {
  std::filesystem::path projectPath;
  std::filesystem::path openSource;
  int maximumFrames = 0;
  std::filesystem::path profileFrames;
  int maximumSeconds = 0;
  bool startPlay = false;
  bool terrainGraph = false;
  std::optional<std::string> terrainSettingsNode;
};

} // namespace

int main(const int argc, char **argv) {
  CLI::App command{"Open the nearest project or an explicit project path",
                   "demi-editor"};
  EditorOptions options;
  command.add_option("project,--project,-p", options.projectPath);
  command.add_option("--open", options.openSource);
  command.add_option("--max-frames", options.maximumFrames)
      ->check(CLI::NonNegativeNumber);
  command.add_option("--max-seconds", options.maximumSeconds)
      ->check(CLI::NonNegativeNumber);
  command.add_option("--profile-frames", options.profileFrames);
  command.add_flag("--play", options.startPlay);
  command.add_flag("--terrain-graph", options.terrainGraph);
  command.add_option("--terrain-settings-node", options.terrainSettingsNode,
                     "Open node settings and the terrain graph");
  const std::vector<std::string> arguments(argv + 1, argv + argc);
  if (const auto status = demi::cli::parseCommand(command, arguments, 0,
                                                  std::cout, std::cerr)) {
    return *status;
  }
  options.terrainGraph =
      options.terrainGraph || options.terrainSettingsNode.has_value();

  std::filesystem::path projectPath = options.projectPath;
  if (projectPath.empty())
    projectPath = demi::findProjectFile(std::filesystem::current_path());
  if (projectPath.empty()) {
    std::cerr << "No demi.project.json was found. Pass --project <path>.\n";
    return 1;
  }

  demi::editor::EditorWorkspace workspace;
  std::string error;
  if (!workspace.open(projectPath, error)) {
    std::cerr << "Could not open editor project: " << error << '\n';
    return 1;
  }

  std::ofstream profile;
  if (!options.profileFrames.empty()) {
    profile.open(options.profileFrames);
    if (!profile) {
      std::cerr << "Cannot open editor frame profile: " << options.profileFrames
                << '\n';
      return 1;
    }
    demi::runtime::RuntimeProfiler::setEnabled(true);
    demi::runtime::RuntimeProfiler::resetSession();
  }
  auto ui = demi::editor::createEditorUiHost();
  const std::string title =
      "Demi Engine Editor - " + workspace.project().project.name;
  if (!ui->initialize(title, error)) {
    std::cerr << "Could not start editor UI: " << error << '\n';
    return 1;
  }

  demi::editor::applyEditorTheme();
  demi::editor::EditorShell shell(workspace);
  if (std::string diagnostic = ui->takeWorkspaceDiagnostic();
      !diagnostic.empty())
    shell.setNotice(std::move(diagnostic));
  if (!ui->loadBranding(error))
    shell.setNotice("About logo unavailable: " + error);
  else
    shell.setBrandingTextureIndex(ui->brandingTextureIndex());
  if (!options.openSource.empty()) {
    std::filesystem::path source = options.openSource;
    if (source.is_relative())
      source = workspace.project().project.projectDirectory / source;
    if (!shell.openDocument(source.lexically_normal(), error))
      shell.setNotice("Could not open source: " + error);
  }
  if (options.terrainGraph) {
    const bool opened =
        options.terrainSettingsNode
            ? shell.openTerrainNodeSettings(*options.terrainSettingsNode, error)
            : shell.openTerrainGraph(error);
    if (!opened) {
      std::cerr << "Could not open terrain graph/settings: " << error << '\n';
      shell.releaseUiResources();
      ui->shutdown();
      return 1;
    }
  }
  for (const auto view : demi::editor::EditorAuthoringViews)
    if (!ui->configureViewport(
            view, workspace.project().project.projectDirectory, error))
      shell.setNotice("Authored view unavailable: " + error);
  shell.playSession().setHostServices(ui->embeddedHostServices());
  if (options.startPlay && !shell.startEmbeddedPlay(error)) {
    std::cerr << "Could not start embedded Play: " << error << '\n';
    shell.releaseUiResources();
    ui->shutdown();
    return 1;
  }
  bool gameRendererReady = false;
  int frame = 0;
  bool profileFailed = false;
  const auto loopStarted = std::chrono::steady_clock::now();
  while (!shell.wantsExit() &&
         (options.maximumFrames <= 0 || frame < options.maximumFrames)) {
    if (options.maximumSeconds > 0 &&
        std::chrono::steady_clock::now() - loopStarted >=
            std::chrono::seconds(options.maximumSeconds))
      break;
    const auto backgroundStarted = std::chrono::steady_clock::now();
    const bool changed = shell.pollBackgroundChanges();
    const auto awaitStarted = std::chrono::steady_clock::now();
    if (!ui->awaitFrame(changed || shell.needsContinuousFrames() ||
                        ui->shouldClose()))
      continue;
    if (ui->shouldClose()) {
      shell.requestExit();
      ui->acknowledgeCloseRequest();
    }
    const auto frameStarted = std::chrono::steady_clock::now();
    const bool embeddedFrame = shell.playSession().isEmbedded();
    if (embeddedFrame) {
      shell.playSession().beginProfilerFrame();
    } else if (profile.is_open()) {
      demi::runtime::RuntimeProfiler::setEnabled(true);
      demi::runtime::RuntimeProfiler::beginFrame();
    }
    demi::runtime::RuntimeProfiler::record(
        "Editor.background_poll", std::chrono::duration<double, std::milli>(
            awaitStarted - backgroundStarted).count());
    demi::runtime::RuntimeProfiler::record(
        "Editor.await_frame", std::chrono::duration<double, std::milli>(
            frameStarted - awaitStarted).count());
    ui->setUiScale(shell.uiScale());
    if (!ui->beginFrame(error)) {
      std::cerr << "Editor frame failed: " << error << '\n';
      // Runtime host callbacks borrow the Game renderer; stop before UI
      // teardown.
      shell.playSession().stop();
      shell.releaseUiResources();
      ui->shutdown();
      return 1;
    }
    for (std::filesystem::path &dropped : ui->takeDroppedFiles())
      shell.queueAssetImport(std::move(dropped));
    std::array<bool, demi::editor::EditorAuthoringViews.size()>
        viewportTargetsReady{};
    for (const auto view : demi::editor::EditorAuthoringViews) {
      const auto &state =
          shell.authoringViews()[static_cast<std::size_t>(view)];
      if (state.workspace && state.area.width && state.area.height) {
        viewportTargetsReady[static_cast<std::size_t>(view)] =
            ui->prepareViewportTarget(view, state.area, error);
        if (!viewportTargetsReady[static_cast<std::size_t>(view)])
          shell.setNotice("Authored view target unavailable: " + error);
      } else if (shell.authoringViewOpen(view)) {
        ui->observeViewportTarget(view, {});
      } else {
        ui->releaseViewport(view);
      }
      shell.setViewportTextureIndex(view, ui->viewportTextureIndex(view));
      shell.setViewportPreparing(view, ui->viewportPreparing(view));
    }
    if (gameRendererReady && !ui->prepareGameTarget(shell.gameArea(), error)) {
      shell.playSession().reportFailure(error);
      ui->releaseGameRenderer();
      gameRendererReady = false;
      shell.setNotice("Game target stopped: " + error);
    }
    shell.setGameTextureIndex(ui->gameTextureIndex());
    shell.playSession().setGpuTiming(ui->gpuTimingSample());
    const auto uiStarted = std::chrono::steady_clock::now();
    {
      demi::runtime::ProfileScope scope("Editor.ui");
      shell.draw(ui->width(), ui->height(), ui->rendererName());
    }
    const double uiMilliseconds = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - uiStarted)
                                      .count();
    if (shell.playSession().isEmbedded() && !gameRendererReady) {
      gameRendererReady = ui->configureGameRenderer(
          workspace.project().project.projectDirectory, error);
      if (!gameRendererReady) {
        shell.playSession().reportFailure(error);
        shell.setNotice("Game view unavailable: " + error);
      }
    }
    if (shell.playSession().isEmbedded()) {
      const demi::editor::EditorViewportArea area = shell.gameArea();
      demi::runtime::InputState gameInput =
          ui->gameInput(area, shell.gameViewFocused());
      const std::uint16_t gameWidth = area.width == 0 ? 960 : area.width;
      const std::uint16_t gameHeight = area.height == 0 ? 540 : area.height;
      const bool stepRequested = shell.takeStepRequest();
      const bool advanced =
          stepRequested
              ? shell.playSession().step(std::move(gameInput), gameWidth,
                                         gameHeight, error)
              : shell.playSession().update(std::move(gameInput),
                                           ui->deltaSeconds(), gameWidth,
                                           gameHeight, error);
      if (!advanced) {
        shell.setNotice("Play session failed: " + error);
      }
      if (!embeddedFrame) {
        // Starting Play during UI dispatch creates its first profiler frame.
        // Preserve the UI cost that preceded that transition.
        demi::runtime::RuntimeProfiler::record("Editor.ui", uiMilliseconds);
      }
    }
    const bool gameOwnsPointer = shell.showingGameView() &&
                                 shell.gameViewFocused() &&
                                 shell.playSession().isEmbedded();
    const bool overGame = ui->gamePointerInside(shell.gameArea());
    if (!ui->setViewportInputCaptured(shell.viewportInputCaptured(), error,
                                      gameOwnsPointer &&
                                          shell.playSession().mouseCaptured(),
                                      !gameOwnsPointer || !overGame ||
                                          shell.playSession().mouseVisible())) {
      shell.setNotice("Viewport input capture failed: " + error);
    }
    if (gameRendererReady && shell.gameArea().width &&
        shell.playSession().runtimeWorld() &&
        !ui->renderGame(*shell.playSession().runtimeWorld(), shell.gameArea(),
                        shell.playSession().interpolationAlpha(), error)) {
      shell.playSession().reportFailure(error);
      shell.setNotice("Game view stopped: " + error);
    }
    for (const auto view : demi::editor::EditorAuthoringViews) {
      const auto &state =
          shell.authoringViews()[static_cast<std::size_t>(view)];
      const auto requestedArea = state.workspace
                                     ? state.area
                                     : demi::editor::EditorViewportArea{};
      ui->observeViewportTarget(view, requestedArea);
      if (!state.workspace || !state.area.width || !state.area.height)
        continue;
      // Docked view rectangles first become available during shell.draw(). A
      // newly opened view has no target until the next frame's preparation;
      // observeViewportTarget keeps that work scheduled after focus settles.
      // Preparation and rendering failures still report their owning errors.
      if (!viewportTargetsReady[static_cast<std::size_t>(view)] ||
          ui->viewportTextureIndex(view) == UINT16_MAX)
        continue;
      auto &document = *state.workspace;
      if (view != demi::editor::EditorAuthoringView::Hud &&
          !document.updateRegionalPreview(error, ui->deltaSeconds())) {
        shell.setNotice("Terrain preview preparation failed: " + error);
      }
      bool rendered = false;
      if (view == demi::editor::EditorAuthoringView::Hud)
        rendered =
            ui->renderHud(view, document.displayedHud(), state.area, error);
      else if (document.viewDimension() ==
               demi::editor::EditorSceneViewDimension::TwoDimensional)
        rendered =
            ui->renderViewport2D(view, document.project().world, state.area,
                                 document.sceneView2D().camera(),
                                 document.sceneView2D().showColliders, error);
      else
        rendered =
            ui->renderViewport(view, document.project().world, state.area,
                               document.sceneView().camera(), error);
      if (!rendered)
        shell.setNotice("Authored view unavailable: " + error);
    }
    {
      demi::runtime::ProfileScope scope("Editor.present");
      ui->endFrame();
    }
    if (auto diagnostic = ui->takeWorkspaceDiagnostic(); !diagnostic.empty()) {
      shell.setNotice(std::move(diagnostic));
    }
    // shell.draw() may stop Play after emitting an Image of the game texture.
    // Submit that UI frame before queuing destruction of its GPU resources.
    // Runtime/render failures follow the same lifetime rule.
    if (!shell.playSession().isEmbedded() && gameRendererReady) {
      ui->releaseGameRenderer();
      gameRendererReady = false;
    }
    if (profile.is_open()) {
      demi::runtime::RuntimeProfiler::setEnabled(true);
      demi::runtime::RuntimeProfiler::setGauge(
          "Editor.playing", shell.playSession().isEmbedded() ? 1.0 : 0.0);
      demi::runtime::RuntimeProfiler::record(
          "Editor.frame", std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - frameStarted)
                              .count());
      if (!demi::runtime::RuntimeProfiler::writeFrame(profile, frame)) {
        profileFailed = true;
        break;
      }
    }
    ++frame;
  }
  shell.playSession().stop();
  if (gameRendererReady)
    ui->releaseGameRenderer();
  shell.releaseUiResources();
  ui->shutdown();
  if (profile.is_open()) {
    profile.flush();
    profileFailed |= !profile;
  }
  if (profileFailed) {
    std::cerr << "Could not write editor frame profile: "
              << options.profileFrames << '\n';
    return 1;
  }
  return 0;
}
