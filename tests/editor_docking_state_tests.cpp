#include "editor/EditorDockingState.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;

  const std::string pattern =
      (fs::temp_directory_path() / "demi-editor-docking-state-XXXXXX").string();
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const char *created = ::mkdtemp(buffer.data());
  assert(created != nullptr);
  const fs::path root = created;
  std::error_code cleanupError;

  // Independent panels start a fresh alpha layout without touching the previous
  // layout or visibility files in the user's workspace directory.
  fs::create_directories(root / "workspace");
  const fs::path oldLayout = root / "workspace/docking-layout-v1.ini";
  const fs::path oldVisibility = root / "workspace/panels-v1.json";
  {
    std::ofstream layout(oldLayout);
    layout << "previous layout";
    std::ofstream visibility(oldVisibility);
    visibility << R"({"format_version":1,"panels":{"stage":false}})";
  }

  EditorDockingStateStore store(root);
  assert(store.layoutPath().filename() == "docking-layout-v2.ini");
  assert(store.visibilityPath().filename() == "panels-v2.json");
  const EditorLayoutPreparation missing = store.prepareLayout();
  assert(!missing.hasSavedLayout && !missing.recoveredCorruptLayout);

  EditorPanelVisibility saved;
  std::string error;
  EditorPanelVisibility loaded;
  assert(store.loadVisibility(loaded, error));
  assert(loaded == EditorPanelVisibility{});
  std::set<std::string_view> names;
  std::set<std::string_view> keys;
  assert(editorPanelDefinitions().size() == 15);
  for (const EditorPanelDefinition &panel : editorPanelDefinitions()) {
    assert(names.insert(panel.windowName).second);
    assert(keys.insert(panel.visibilityKey).second);
    assert(loaded.*panel.visible);
    saved = {};
    saved.*panel.visible = false;
    assert(store.saveVisibility(saved, error));
    assert(store.loadVisibility(loaded, error));
    assert(loaded == saved);
  }
  assert(!names.contains("Stage") && !keys.contains("stage"));
  for (const EditorPanelDefinition &panel : editorPanelDefinitions()) {
    if (panel.visibilityKey == "ui_palette")
      assert(panel.availability == EditorPanelAvailability::Hud);
    else if (panel.visibilityKey == "terrain_nodes")
      assert(panel.availability == EditorPanelAvailability::TerrainGraph);
    else
      assert(panel.availability == EditorPanelAvailability::Always);
  }
  for (const EditorPanelDefinition &panel : editorPanelDefinitions())
    saved.*panel.visible = false;
  assert(store.saveVisibility(saved, error));
  assert(store.loadVisibility(loaded, error));
  assert(loaded == saved);
  {
    std::ifstream input(store.visibilityPath());
    const auto document = nlohmann::json::parse(input);
    assert(document.at("format_version") == 2);
    assert(document.at("panels").size() == 15);
    assert(!document.at("panels").contains("stage"));
  }
  EditorDockingStateStore otherUser(root / "other-user");
  EditorPanelVisibility isolated;
  assert(otherUser.loadVisibility(isolated, error));
  assert(isolated == EditorPanelVisibility{});

  fs::create_directories(store.layoutPath().parent_path());
  {
    std::ofstream output(store.layoutPath());
    output << "[Window][Viewport]\nDockId=0x01\n\n[Docking][Data]\n"
              "DockSpace ID=0x01 Size=800,600\n";
  }
  const EditorLayoutPreparation valid = store.prepareLayout();
  assert(valid.hasSavedLayout && !valid.recoveredCorruptLayout);

  {
    std::ofstream output(store.layoutPath(),
                         std::ios::binary | std::ios::trunc);
    output << "not an imgui docking layout";
  }
  const EditorLayoutPreparation corrupt = store.prepareLayout();
  assert(!corrupt.hasSavedLayout && corrupt.recoveredCorruptLayout);
  assert(!corrupt.diagnostic.empty());
  assert(!fs::exists(store.layoutPath()));
  assert(fs::exists(store.layoutPath().string() + ".corrupt"));

  {
    std::ofstream output(store.visibilityPath(), std::ios::trunc);
    output << "{\"format_version\":99}";
  }
  loaded = {};
  error.clear();
  assert(!store.loadVisibility(loaded, error));
  assert(!error.empty() && loaded == EditorPanelVisibility{});

  // Invalid late fields must not leave a partially restored visibility state.
  {
    std::ofstream output(store.visibilityPath(), std::ios::trunc);
    output
        << R"({"format_version":2,"panels":{"viewport":false,"assets":"invalid"}})";
  }
  loaded = saved;
  assert(!store.loadVisibility(loaded, error));
  assert(loaded == EditorPanelVisibility{});
  {
    std::ifstream layout(oldLayout);
    std::string text;
    std::getline(layout, text);
    assert(text == "previous layout");
    std::ifstream visibility(oldVisibility);
    const auto document = nlohmann::json::parse(visibility);
    assert(document.at("format_version") == 1);
    assert(document.at("panels").at("stage") == false);
  }

  fs::remove_all(root, cleanupError);
  assert(!cleanupError);
  return 0;
}
