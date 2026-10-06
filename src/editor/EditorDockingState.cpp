#include "editor/EditorDockingState.h"

#include "editor/EditorDocumentStore.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <utility>

namespace demi::editor {

namespace {

constexpr std::uintmax_t MaximumLayoutBytes = 2U * 1024U * 1024U;

constexpr std::array PanelDefinitions{
    EditorPanelDefinition{"Hierarchy", "hierarchy",
                          &EditorPanelVisibility::hierarchy,
                          EditorPanelDockGroup::Hierarchy},
    EditorPanelDefinition{"Viewport", "viewport",
                          &EditorPanelVisibility::viewport,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"Prefab", "prefab", &EditorPanelVisibility::prefab,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"HUD", "hud", &EditorPanelVisibility::hud,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"Terrain Graph", "terrain_graph",
                          &EditorPanelVisibility::terrainGraph,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"Terrain Asset", "terrain_asset",
                          &EditorPanelVisibility::terrainAsset,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"Game View", "game", &EditorPanelVisibility::game,
                          EditorPanelDockGroup::Authoring},
    EditorPanelDefinition{"Inspector", "inspector",
                          &EditorPanelVisibility::inspector,
                          EditorPanelDockGroup::Properties},
    EditorPanelDefinition{
        "UI Palette", "ui_palette", &EditorPanelVisibility::uiPalette,
        EditorPanelDockGroup::Properties, EditorPanelAvailability::Hud},
    EditorPanelDefinition{"Terrain Nodes", "terrain_nodes",
                          &EditorPanelVisibility::terrainNodes,
                          EditorPanelDockGroup::Properties,
                          EditorPanelAvailability::TerrainGraph},
    EditorPanelDefinition{"Console", "console", &EditorPanelVisibility::console,
                          EditorPanelDockGroup::Diagnostics},
    EditorPanelDefinition{"Lua Console", "lua_console",
                          &EditorPanelVisibility::luaConsole,
                          EditorPanelDockGroup::Diagnostics},
    EditorPanelDefinition{"Profiler", "profiler",
                          &EditorPanelVisibility::profiler,
                          EditorPanelDockGroup::Diagnostics},
    EditorPanelDefinition{"Debug", "debug", &EditorPanelVisibility::debug,
                          EditorPanelDockGroup::Diagnostics},
    EditorPanelDefinition{"Assets", "assets", &EditorPanelVisibility::assets,
                          EditorPanelDockGroup::Assets}};

std::filesystem::path
availableQuarantinePath(const std::filesystem::path &layoutPath,
                        std::error_code &error) {
  for (int suffix = 0; suffix < 100; ++suffix) {
    std::filesystem::path candidate = layoutPath;
    candidate +=
        suffix == 0 ? ".corrupt" : ".corrupt." + std::to_string(suffix);
    const bool exists = std::filesystem::exists(candidate, error);
    if (error)
      return {};
    if (!exists)
      return candidate;
  }
  return {};
}

} // namespace

std::span<const EditorPanelDefinition> editorPanelDefinitions() {
  return PanelDefinitions;
}

EditorDockingStateStore::EditorDockingStateStore(
    std::filesystem::path editorDataRoot)
    : layoutPath_(std::move(editorDataRoot) / "workspace" /
                  "docking-layout-v2.ini"),
      visibilityPath_(layoutPath_.parent_path() / "panels-v2.json") {}

const std::filesystem::path &
EditorDockingStateStore::layoutPath() const noexcept {
  return layoutPath_;
}

const std::filesystem::path &
EditorDockingStateStore::visibilityPath() const noexcept {
  return visibilityPath_;
}

EditorLayoutPreparation EditorDockingStateStore::prepareLayout() const {
  EditorLayoutPreparation result;
  std::error_code error;
  const bool exists = std::filesystem::exists(layoutPath_, error);
  if (error) {
    result.diagnostic =
        "Could not inspect workspace layout: " + error.message();
    return result;
  }
  if (!exists)
    return result;

  const bool isRegular = std::filesystem::is_regular_file(layoutPath_, error);
  const std::uintmax_t size =
      isRegular ? std::filesystem::file_size(layoutPath_, error) : 0;
  std::string contents;
  if (!error && isRegular && size <= MaximumLayoutBytes) {
    std::ifstream input(layoutPath_, std::ios::binary);
    contents.assign(std::istreambuf_iterator<char>(input), {});
  }
  const bool valid = !error && isRegular && size <= MaximumLayoutBytes &&
                     contents.find('\0') == std::string::npos &&
                     contents.find("[Docking][Data]") != std::string::npos;
  if (valid) {
    result.hasSavedLayout = true;
    return result;
  }

  error.clear();
  const std::filesystem::path quarantine =
      availableQuarantinePath(layoutPath_, error);
  if (quarantine.empty()) {
    result.diagnostic =
        error ? "Workspace layout quarantine is unavailable: " + error.message()
              : "Workspace layout is invalid and no quarantine filename is "
                "available.";
    return result;
  }
  std::filesystem::rename(layoutPath_, quarantine, error);
  if (error) {
    result.diagnostic = "Workspace layout is invalid and could not be "
                        "quarantined: " +
                        error.message();
    return result;
  }
  result.recoveredCorruptLayout = true;
  result.diagnostic = "Invalid workspace layout was preserved as " +
                      quarantine.filename().string() +
                      "; the default workspace was restored.";
  return result;
}

bool EditorDockingStateStore::loadVisibility(EditorPanelVisibility &visibility,
                                             std::string &error) const {
  visibility = {};
  try {
    if (!std::filesystem::exists(visibilityPath_))
      return true;
    std::ifstream input(visibilityPath_);
    const nlohmann::json document = nlohmann::json::parse(input);
    if (document.value("format_version", 0) != 2 ||
        !document.contains("panels") || !document["panels"].is_object()) {
      error = "Workspace panel state uses an unsupported format.";
      return false;
    }
    const nlohmann::json &panels = document["panels"];
    EditorPanelVisibility restored;
    for (const EditorPanelDefinition &panel : PanelDefinitions)
      restored.*panel.visible =
          panels.value(std::string(panel.visibilityKey), true);
    visibility = restored;
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorDockingStateStore::saveVisibility(
    const EditorPanelVisibility &visibility, std::string &error) const {
  std::error_code directoryError;
  std::filesystem::create_directories(visibilityPath_.parent_path(),
                                      directoryError);
  if (directoryError) {
    error = "Could not create workspace state directory: " +
            directoryError.message();
    return false;
  }
  nlohmann::json panels = nlohmann::json::object();
  for (const EditorPanelDefinition &panel : PanelDefinitions)
    panels[std::string(panel.visibilityKey)] = visibility.*panel.visible;
  const nlohmann::json document{{"format_version", 2},
                                {"panels", std::move(panels)}};
  return EditorDocumentStore::writeAtomically(visibilityPath_,
                                              document.dump(2) + '\n', error);
}

} // namespace demi::editor
