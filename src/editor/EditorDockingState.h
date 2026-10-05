#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace demi::editor {

struct EditorPanelVisibility {
  bool hierarchy = true;
  bool viewport = true;
  bool prefab = true;
  bool hud = true;
  bool terrainGraph = true;
  bool terrainAsset = true;
  bool game = true;
  bool inspector = true;
  bool uiPalette = true;
  bool terrainNodes = true;
  bool console = true;
  bool luaConsole = true;
  bool profiler = true;
  bool debug = true;
  bool assets = true;
  auto operator<=>(const EditorPanelVisibility &) const = default;
};

enum class EditorPanelDockGroup {
  Hierarchy,
  Authoring,
  Properties,
  Diagnostics,
  Assets
};
enum class EditorPanelAvailability { Always, Hud, TerrainGraph };

// Stable window names and persistence keys are shared by the View menu,
// visibility store and default dock builder. Document availability is separate
// from the user's visibility preference.
struct EditorPanelDefinition {
  std::string_view windowName;
  std::string_view visibilityKey;
  bool EditorPanelVisibility::*visible;
  EditorPanelDockGroup dockGroup;
  EditorPanelAvailability availability = EditorPanelAvailability::Always;
};

[[nodiscard]] std::span<const EditorPanelDefinition> editorPanelDefinitions();

struct EditorLayoutPreparation {
  bool hasSavedLayout = false;
  bool recoveredCorruptLayout = false;
  std::string diagnostic;
};

class EditorDockingStateStore {
public:
  explicit EditorDockingStateStore(std::filesystem::path editorDataRoot);

  [[nodiscard]] const std::filesystem::path &layoutPath() const noexcept;
  [[nodiscard]] const std::filesystem::path &visibilityPath() const noexcept;
  [[nodiscard]] EditorLayoutPreparation prepareLayout() const;
  [[nodiscard]] bool loadVisibility(EditorPanelVisibility &visibility,
                                    std::string &error) const;
  [[nodiscard]] bool saveVisibility(const EditorPanelVisibility &visibility,
                                    std::string &error) const;

private:
  std::filesystem::path layoutPath_;
  std::filesystem::path visibilityPath_;
};

} // namespace demi::editor
