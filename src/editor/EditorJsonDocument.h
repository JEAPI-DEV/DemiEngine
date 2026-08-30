#pragma once

#include "editor/EditorDocumentStore.h"

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

using EditorJsonValidator = std::function<Diagnostics(
    const std::filesystem::path &, const nlohmann::json &)>;

// Shared persistence and history for multiple proven versioned JSON document
// kinds. Format interpretation stays in specialized adapters and validators.
class EditorJsonDocument {
public:
  [[nodiscard]] bool open(std::filesystem::path path,
                          EditorJsonValidator validator, std::string &error);
  [[nodiscard]] bool set(std::string_view pointer, nlohmann::json value,
                         std::string &error);
  [[nodiscard]] bool erase(std::string_view pointer, std::string &error);
  [[nodiscard]] bool replace(nlohmann::json document, std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool save(std::string &error);

  [[nodiscard]] const nlohmann::json &json() const { return document_; }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }
  [[nodiscard]] const Diagnostics &diagnostics() const { return diagnostics_; }
  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

private:
  struct Change {
    nlohmann::json before;
    nlohmann::json after;
  };
  [[nodiscard]] bool commit(nlohmann::json replacement, std::string &error);

  EditorDocumentStore store_;
  EditorJsonValidator validator_;
  std::filesystem::path path_;
  FileRevision revision_;
  nlohmann::json document_;
  std::string savedCanonical_;
  Diagnostics diagnostics_;
  std::vector<Change> undo_;
  std::vector<Change> redo_;
};

} // namespace demi::editor
