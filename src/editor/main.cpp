#include "editor/EditorShell.h"
#include "editor/EditorTheme.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectDiscovery.h"

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
  bool showHelp = false;
  bool terrainGraph = false;
  std::optional<std::string> terrainSettingsNode;
  std::string optionError;
};

EditorOptions parseOptions(const int argc, char **argv) {
  EditorOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if ((argument == "--project" || argument == "-p") && index + 1 < argc) {
      options.projectPath = argv[++index];
    } else if (argument == "--max-frames" && index + 1 < argc) {
      try {
        options.maximumFrames = std::max(0, std::stoi(argv[++index]));
      } catch (const std::exception &) {
        options.maximumFrames = 0;
      }
    } else if (argument == "--open" && index + 1 < argc) {
      options.openSource = argv[++index];
    } else if (argument == "--terrain-graph") {
      options.terrainGraph = true;
    } else if (argument == "--terrain-settings-node") {
      if (index + 1 >= argc || std::string_view(argv[index + 1]).empty() ||
          std::string_view(argv[index + 1]).starts_with("--")) {
        options.optionError =
            "--terrain-settings-node requires a stable terrain graph node ID.";
        return options;
      }
      options.terrainSettingsNode = argv[++index];
      options.terrainGraph = true;
    } else if (argument == "--help" || argument == "-h") {
      options.showHelp = true;
    } else if (!argument.starts_with('-') && options.projectPath.empty()) {
      options.projectPath = argument;
    }
  }
  return options;
}

void printHelp() {
  std::cout << "Usage: demi-editor [--project <demi.project.json|directory>]\n"
               "                   [--open <authored-source>]\n"
               "                   [--terrain-graph]\n"
               "                   [--terrain-settings-node <stable-node-id>]\n"
               "                   [--max-frames <count>]\n\n"
               "Without --project, the nearest parent demi.project.json is "
               "opened.\n"
               "--terrain-graph opens the graph for the selected terrain "
               "after --open.\n"
               "--terrain-settings-node opens that graph node's settings "
               "and implies --terrain-graph.\n";
}

} // namespace

int main(const int argc, char **argv) {
  const EditorOptions options = parseOptions(argc, argv);
  if (options.showHelp) {
    printHelp();
    return 0;
  }
  if (!options.optionError.empty()) {
    std::cerr << options.optionError << '\n';
    return 1;
  }

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
  bool gameRendererReady = false;
  int frame = 0;
  while (!shell.wantsExit() &&
         (options.maximumFrames <= 0 || frame < options.maximumFrames)) {
    if (ui->shouldClose()) {
      shell.requestExit();
      ui->acknowledgeCloseRequest();
    }
    ui->setUiScale(shell.uiScale());
    if (!ui->beginFrame(error)) {
      std::cerr << "Editor frame failed: " << error << '\n';
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
      } else {
        ui->releaseViewport(view);
      }
      shell.setViewportTextureIndex(view, ui->viewportTextureIndex(view));
    }
    if (gameRendererReady && !ui->prepareGameTarget(shell.gameArea(), error)) {
      shell.playSession().reportFailure(error);
      ui->releaseGameRenderer();
      gameRendererReady = false;
      shell.setNotice("Game target stopped: " + error);
    }
    shell.setGameTextureIndex(ui->gameTextureIndex());
    shell.playSession().setGpuTiming(ui->gpuTimingSample());
    shell.draw(ui->width(), ui->height(), ui->rendererName());
    if (shell.playSession().isEmbedded() && !gameRendererReady) {
      gameRendererReady = ui->configureGameRenderer(
          workspace.project().project.projectDirectory, error);
      if (!gameRendererReady) {
        shell.playSession().reportFailure(error);
        shell.setNotice("Game view unavailable: " + error);
      }
    } else if (!shell.playSession().isEmbedded() && gameRendererReady) {
      ui->releaseGameRenderer();
      gameRendererReady = false;
    }
    if (shell.playSession().isEmbedded()) {
      const demi::editor::EditorViewportArea area = shell.gameArea();
      demi::runtime::InputState gameInput =
          ui->gameInput(area, shell.gameViewFocused());
      const std::uint16_t gameWidth = area.width == 0 ? 960 : area.width;
      const std::uint16_t gameHeight = area.height == 0 ? 540 : area.height;
      const bool advanced =
          shell.takeStepRequest()
              ? shell.playSession().step(std::move(gameInput), gameWidth,
                                         gameHeight, error)
              : shell.playSession().update(std::move(gameInput),
                                           ui->deltaSeconds(), gameWidth,
                                           gameHeight, error);
      if (!advanced)
        shell.setNotice("Play session failed: " + error);
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
      ui->releaseGameRenderer();
      gameRendererReady = false;
      shell.setNotice("Game view stopped: " + error);
    }
    for (const auto view : demi::editor::EditorAuthoringViews) {
      const auto &state =
          shell.authoringViews()[static_cast<std::size_t>(view)];
      if (!state.workspace || !state.area.width || !state.area.height)
        continue;
      // Docked view rectangles first become available during shell.draw(). A
      // newly opened view has no target until the next frame's preparation.
      // Preparation and rendering failures still report their owning errors.
      if (!viewportTargetsReady[static_cast<std::size_t>(view)] ||
          ui->viewportTextureIndex(view) == UINT16_MAX)
        continue;
      const auto &document = *state.workspace;
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
    ui->endFrame();
    ++frame;
  }
  shell.playSession().stop();
  if (gameRendererReady)
    ui->releaseGameRenderer();
  shell.releaseUiResources();
  ui->shutdown();
  return 0;
}
