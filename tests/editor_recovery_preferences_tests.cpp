#include "editor/EditorPreferencesStore.h"
#include "editor/EditorRecoveryStore.h"
#include "editor/EditorWorkspace.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  const fs::path temporary =
      fs::temp_directory_path() / "demi-editor-recovery-preferences";
  std::error_code ignored;
  fs::remove_all(temporary, ignored);
  fs::create_directories(temporary);
  const fs::path project = temporary / "project/demi.project.json";

  EditorRecoveryStore recovery(temporary / "cache");
  std::string error;
  const std::vector<EditorRecoveryDocument> documents{
      {.path = temporary / "project/scenes/main.scene.json",
       .kind = "scene",
       .content = {{"format_version", 1},
                   {"entities", nlohmann::json::array()}}}};
  assert(recovery.update(project, documents, error));
  assert(recovery.recoveryPath(project).parent_path() ==
         temporary / "cache/recovery");
  const auto loaded = recovery.load(project, error);
  assert(loaded && loaded->documents.size() == 1);
  assert(loaded->documents.front().content == documents.front().content);
  assert(recovery.discard(project, error));
  assert(!recovery.load(project, error));

  EditorPreferencesStore preferencesStore(temporary / "data");
  EditorPreferences preferences{.uiScale = 1.5F,
                                .translationSnap = 0.25F,
                                .rotationSnapDegrees = 5.0F,
                                .scaleSnap = 0.05F,
                                .showBounds3D = true,
                                .showColliders2D = true};
  assert(preferencesStore.save(preferences, error));
  EditorPreferences restored;
  assert(preferencesStore.load(restored, error));
  assert(restored == preferences);
  for (const float invalid :
       {0.0F, 3.0F, std::numeric_limits<float>::infinity()}) {
    auto invalidPreferences = preferences;
    invalidPreferences.uiScale = invalid;
    assert(!preferencesStore.save(invalidPreferences, error));
    assert(preferencesStore.load(restored, error));
    assert(restored == preferences);
  }
  {
    std::ofstream output(temporary / "data/preferences.json");
    output << R"({"format_version":1})";
  }
  assert(preferencesStore.load(restored, error));
  assert(restored.uiScale == 1.0F);

  EditorWorkspace prefabEdited;
  error.clear();
  const fs::path prefabProject =
      fs::path(DEMI_SOURCE_DIR) / "examples/performance_3d_lab";
  assert(prefabEdited.open(prefabProject, error));
  assert(prefabEdited.openPrefabDocument(
      prefabProject / "prefabs/barrel.prefab.json", error));
  assert(prefabEdited.editValue({.entityId = "body", .field = "name"},
                                "Recovered barrel", false, error));
  EditorWorkspace prefabRecovered;
  assert(prefabRecovered.open(prefabProject, error));
  assert(prefabRecovered.applyRecovery(
      {.projectPath = prefabRecovered.projectPath(),
       .documents = prefabEdited.dirtyDocuments()},
      error));
  assert(prefabRecovered.isPrefabDocument());
  assert(prefabRecovered.selectedEntity()->name == "Recovered barrel");
  assert(prefabRecovered.sceneDocument().isDirty());

  const fs::path source = DEMI_SOURCE_DIR;
  EditorWorkspace edited;
  assert(edited.open(source / "examples/minimal_voxel", error));
  const std::string entityId = edited.project().world.entities.front().id;
  assert(edited.editValue({.entityId = entityId, .field = "name"},
                          "Recovered name", false, error));
  const std::vector<EditorRecoveryDocument> workspaceDocuments =
      edited.dirtyDocuments();
  assert(workspaceDocuments.size() == 1);

  EditorWorkspace clean;
  assert(clean.open(source / "examples/minimal_voxel", error));
  assert(clean.applyRecovery(
      {.projectPath = clean.projectPath(), .documents = workspaceDocuments},
      error));
  assert(clean.sceneDocument().isDirty());
  assert(clean.project().world.entities.front().name == "Recovered name");

  fs::remove_all(temporary, ignored);
}
