#include "editor/EditorShell.h"
#include "editor/EditorTheme.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectDiscovery.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct EditorOptions {
  std::filesystem::path projectPath;
  int maximumFrames = 0;
  bool showHelp = false;
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
               "                   [--max-frames <count>]\n\n"
               "Without --project, the nearest parent demi.project.json is "
               "opened.\n";
}

} // namespace

int main(const int argc, char **argv) {
  const EditorOptions options = parseOptions(argc, argv);
  if (options.showHelp) {
    printHelp();
    return 0;
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
  bool viewportReady = ui->configureViewport(
      workspace.project().project.projectDirectory, error);
  if (!viewportReady)
    shell.setNotice("Viewport unavailable: " + error);
  bool gameRendererReady = false;
  int frame = 0;
  while (!ui->shouldClose() && !shell.wantsExit() &&
         (options.maximumFrames <= 0 || frame < options.maximumFrames)) {
    if (!ui->beginFrame(error)) {
      std::cerr << "Editor frame failed: " << error << '\n';
      ui->shutdown();
      return 1;
    }
    if (gameRendererReady && !ui->prepareGameTarget(shell.gameArea(), error)) {
      shell.playSession().reportFailure(error);
      ui->releaseGameRenderer();
      gameRendererReady = false;
      shell.setNotice("Game target stopped: " + error);
    }
    shell.setGameTextureIndex(ui->gameTextureIndex());
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
    if (!ui->setViewportInputCaptured(shell.viewportInputCaptured(), error)) {
      shell.setNotice("Viewport input capture failed: " + error);
    }
    bool rendered = true;
    if (gameRendererReady && shell.showingGameView() &&
        shell.playSession().runtimeWorld() != nullptr) {
      rendered =
          ui->renderGame(*shell.playSession().runtimeWorld(), shell.gameArea(),
                         shell.playSession().interpolationAlpha(), error);
    } else if (viewportReady &&
               workspace.viewDimension() ==
                   demi::editor::EditorSceneViewDimension::TwoDimensional) {
      rendered =
          ui->renderViewport2D(workspace.project().world, shell.viewportArea(),
                               workspace.sceneView2D().camera(),
                               workspace.sceneView2D().showColliders, error);
    } else if (viewportReady) {
      rendered =
          ui->renderViewport(workspace.project().world, shell.viewportArea(),
                             workspace.sceneView().camera(), error);
    }
    if (!rendered) {
      if (shell.showingGameView()) {
        shell.playSession().reportFailure(error);
        ui->releaseGameRenderer();
        gameRendererReady = false;
        shell.setNotice("Game view stopped: " + error);
      } else {
        viewportReady = false;
        shell.setNotice("Viewport stopped: " + error);
      }
    }
    ui->endFrame();
    ++frame;
  }
  shell.playSession().stop();
  if (gameRendererReady)
    ui->releaseGameRenderer();
  ui->shutdown();
  return 0;
}
