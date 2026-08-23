#pragma once

#include "editor/EditorDocumentStore.h"

#include "demi/runtime/scene/model/ProjectData.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

// Owns reversible edits to the versioned project file. Project settings stay
// independent from scene history because they have a different persistence and
// reload boundary.
class EditorProjectDocument {
public:
  [[nodiscard]] bool open(const std::filesystem::path &path,
                          std::string &error);
  [[nodiscard]] bool reload(std::string &error);
  [[nodiscard]] bool save(std::string &error);

  [[nodiscard]] bool setPreloadedAssets(std::vector<std::string> assets,
                                        std::string &error);
  [[nodiscard]] bool addScene(std::string id, std::filesystem::path path,
                              std::string &error);
  [[nodiscard]] bool removeScene(std::string_view id, std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);

  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  [[nodiscard]] bool hasExternalConflict() const {
    return hasExternalConflict_;
  }
  [[nodiscard]] const nlohmann::json &json() const { return document_; }
  [[nodiscard]] std::vector<std::string> preloadedAssets() const;
  [[nodiscard]] std::vector<runtime::SceneEntry> scenes() const;
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  struct Change {
    nlohmann::json before;
    nlohmann::json after;
  };

  [[nodiscard]] bool commit(nlohmann::json replacement, std::string &error);
  [[nodiscard]] bool validate(const nlohmann::json &document,
                              std::string &error) const;

  EditorDocumentStore store_;
  std::filesystem::path path_;
  FileRevision revision_;
  nlohmann::json document_;
  std::string savedCanonical_;
  std::vector<Change> undo_;
  std::vector<Change> redo_;
  bool hasExternalConflict_ = false;
};

} // namespace demi::editor
