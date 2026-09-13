#include "editor/EditorDockingState.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;

  const fs::path root =
      fs::temp_directory_path() / "demi_editor_docking_state_tests";
  std::error_code cleanupError;
  fs::remove_all(root, cleanupError);

  EditorDockingStateStore store(root);
  const EditorLayoutPreparation missing = store.prepareLayout();
  assert(!missing.hasSavedLayout && !missing.recoveredCorruptLayout);

  EditorPanelVisibility saved;
  saved.console = false;
  saved.assets = false;
  std::string error;
  assert(store.saveVisibility(saved, error));
  EditorPanelVisibility loaded;
  assert(store.loadVisibility(loaded, error));
  assert(loaded == saved);
  EditorDockingStateStore otherUser(root.string() + "-other-user");
  EditorPanelVisibility isolated;
  assert(otherUser.loadVisibility(isolated, error));
  assert(isolated == EditorPanelVisibility{});

  fs::create_directories(store.layoutPath().parent_path());
  {
    std::ofstream output(store.layoutPath());
    output << "[Window][Stage]\nDockId=0x01\n\n[Docking][Data]\n"
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

  fs::remove_all(root, cleanupError);
  fs::remove_all(root.string() + "-other-user", cleanupError);
  return 0;
}
