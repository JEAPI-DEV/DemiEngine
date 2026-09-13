#include "cli/build/BuildService.h"
#include "cli/project/ProjectTemplates.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorWorkspace.h"

#include "demi/schema/Validation.h"

#include <cassert>
#include <filesystem>

int main() {
  namespace fs = std::filesystem;
  using namespace demi;
  const fs::path source = DEMI_SOURCE_DIR;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_release_workflow";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);

  Diagnostics diagnostics;
  const auto projectTemplate =
      cli::project::ProjectTemplateCatalog(source / "templates")
          .find("blank-2d", diagnostics);
  assert(projectTemplate && !hasErrors(diagnostics));
  const fs::path projectRoot = root / "project";
  const auto scaffold = cli::project::ProjectScaffolder{}.create(
      {.projectTemplate = *projectTemplate,
       .destination = projectRoot,
       .projectName = "Editor Release Gate"});
  assert(scaffold.committed && !hasErrors(scaffold.diagnostics));

  editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(projectRoot, error));
  assert(workspace.editValue({.entityId = "ent_marker", .field = "name"},
                             "Release Marker", false, error));
  assert(workspace.duplicateEntity("ent_marker", error));
  assert(workspace.sceneDocument().entity("ent_marker_copy") != nullptr);
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().entity("ent_marker_copy") == nullptr);
  assert(workspace.redo(error));
  assert(workspace.addComponent("ent_marker_copy", "AudioListener", error));
  assert(workspace.removeComponent("ent_marker_copy", "AudioListener", error));
  assert(workspace.undo(error));
  assert(workspace.redo(error));
  assert(workspace.saveAll(error));
  assert(!workspace.hasUnsavedChanges());

  const fs::path projectFile = projectRoot / "demi.project.json";
  const auto validation = build::runProjectOperation(
      {.operation = build::ProjectOperation::Validate,
       .projectFile = projectFile});
  assert(validation.succeeded());
  assert(!hasErrors(validatePath(projectRoot).diagnostics));

  editor::EditorPlaySession play;
  assert(play.startEmbedded(projectFile, error));
  for (int frame = 0; frame < 3; ++frame)
    assert(play.update({}, 1.0F / 60.0F, 640, 360, error));
  assert(play.fixedTickCount() > 0);
  play.stop();

  const fs::path cooked = root / "cooked";
  const auto cook = build::runProjectOperation(
      {.operation = build::ProjectOperation::CookLinux,
       .projectFile = projectFile,
       .outputDirectory = cooked});
  assert(cook.succeeded());
  assert(fs::is_regular_file(cooked / "cook.manifest.json"));

  const fs::path package = root / "package";
  const auto packaged = build::runProjectOperation(
      {.operation = build::ProjectOperation::PackageLinux,
       .projectFile = projectFile,
       .outputDirectory = package,
       .runtimeExecutable = DEMI_RUNTIME_PATH});
  assert(packaged.succeeded());
  assert(fs::is_regular_file(package / "bin/editor_release_gate"));
  assert(fs::is_regular_file(package / "project/cook.manifest.json"));

  fs::remove_all(root, ignored);
}
