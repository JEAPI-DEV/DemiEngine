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
  int frame = 0;
  while (!ui->shouldClose() && !shell.wantsExit() &&
         (options.maximumFrames <= 0 || frame < options.maximumFrames)) {
    if (!ui->beginFrame(error)) {
      std::cerr << "Editor frame failed: " << error << '\n';
      ui->shutdown();
      return 1;
    }
    shell.draw(ui->width(), ui->height(), ui->rendererName());
    if (!ui->setViewportInputCaptured(shell.viewportInputCaptured(), error)) {
      shell.setNotice("Viewport input capture failed: " + error);
    }
    if (viewportReady && !ui->renderViewport(workspace.project().world,
                                             shell.viewportArea(),
                                             workspace.sceneView().camera(),
                                             error)) {
      viewportReady = false;
      shell.setNotice("Viewport stopped: " + error);
    }
    ui->endFrame();
    ++frame;
  }
  ui->shutdown();
  return 0;
}
