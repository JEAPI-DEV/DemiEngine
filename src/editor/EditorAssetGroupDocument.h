#pragma once

#include "editor/EditorDocumentStore.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace demi::editor {

class EditorAssetGroupDocument {
public:
  [[nodiscard]] bool open(const std::filesystem::path &path,
                          std::string &error);
  [[nodiscard]] bool setRoots(std::vector<std::string> roots,
                              std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool save(std::string &error);

  [[nodiscard]] const std::string &id() const { return id_; }
  [[nodiscard]] std::vector<std::string> roots() const;
  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  struct Change {
    nlohmann::json before;
    nlohmann::json after;
  };
  [[nodiscard]] bool valid(const nlohmann::json &document,
                           std::string &error) const;

  EditorDocumentStore store_;
  std::filesystem::path path_;
  FileRevision revision_;
  nlohmann::json document_;
  std::string savedCanonical_;
  std::string id_;
  std::vector<Change> undo_;
  std::vector<Change> redo_;
};

} // namespace demi::editor
