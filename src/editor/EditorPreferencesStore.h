#pragma once

#include <filesystem>
#include <string>

namespace demi::editor {

struct EditorPreferences {
  float translationSnap = 1.0F;
  float rotationSnapDegrees = 15.0F;
  float scaleSnap = 0.1F;
  bool showBounds3D = false;
  bool showColliders3D = false;
  bool showLights3D = true;
  bool showCameras3D = true;
  bool showGrid2D = true;
  bool showBounds2D = true;
  bool showColliders2D = false;
  bool showCameras2D = true;
  auto operator<=>(const EditorPreferences &) const = default;
};

class EditorPreferencesStore {
public:
  explicit EditorPreferencesStore(std::filesystem::path root);
  [[nodiscard]] bool load(EditorPreferences &preferences,
                          std::string &error) const;
  [[nodiscard]] bool save(const EditorPreferences &preferences,
                          std::string &error) const;

private:
  std::filesystem::path path_;
};

} // namespace demi::editor
