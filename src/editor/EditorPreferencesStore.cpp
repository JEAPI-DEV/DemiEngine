#include "editor/EditorPreferencesStore.h"

#include "editor/EditorDocumentStore.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace demi::editor {

EditorPreferencesStore::EditorPreferencesStore(std::filesystem::path root)
    : path_(std::move(root) / "preferences.json") {}

bool EditorPreferencesStore::load(EditorPreferences &preferences,
                                  std::string &error) const {
  preferences = {};
  if (!std::filesystem::exists(path_))
    return true;
  try {
    std::ifstream input(path_);
    const nlohmann::json document = nlohmann::json::parse(input);
    if (document.value("format_version", 0) != 1) {
      error = "Editor preferences use an unsupported format version.";
      return false;
    }
    preferences.translationSnap = document.value("translation_snap", 1.0F);
    preferences.rotationSnapDegrees =
        document.value("rotation_snap_degrees", 15.0F);
    preferences.scaleSnap = document.value("scale_snap", 0.1F);
    preferences.showBounds3D = document.value("show_bounds_3d", false);
    preferences.showColliders3D = document.value("show_colliders_3d", false);
    preferences.showLights3D = document.value("show_lights_3d", true);
    preferences.showCameras3D = document.value("show_cameras_3d", true);
    preferences.showGrid2D = document.value("show_grid_2d", true);
    preferences.showBounds2D = document.value("show_bounds_2d", true);
    preferences.showColliders2D = document.value("show_colliders_2d", false);
    preferences.showCameras2D = document.value("show_cameras_2d", true);
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorPreferencesStore::save(const EditorPreferences &preferences,
                                  std::string &error) const {
  std::error_code directoryError;
  std::filesystem::create_directories(path_.parent_path(), directoryError);
  if (directoryError) {
    error = "Could not create editor preference directory: " +
            directoryError.message();
    return false;
  }
  const nlohmann::json document{
      {"format_version", 1},
      {"translation_snap", preferences.translationSnap},
      {"rotation_snap_degrees", preferences.rotationSnapDegrees},
      {"scale_snap", preferences.scaleSnap},
      {"show_bounds_3d", preferences.showBounds3D},
      {"show_colliders_3d", preferences.showColliders3D},
      {"show_lights_3d", preferences.showLights3D},
      {"show_cameras_3d", preferences.showCameras3D},
      {"show_grid_2d", preferences.showGrid2D},
      {"show_bounds_2d", preferences.showBounds2D},
      {"show_colliders_2d", preferences.showColliders2D},
      {"show_cameras_2d", preferences.showCameras2D}};
  return EditorDocumentStore::writeAtomically(path_, document.dump(2) + '\n',
                                              error);
}

} // namespace demi::editor
