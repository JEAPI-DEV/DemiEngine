#pragma once

#include "editor/EditorDocumentStore.h"
#include "editor/EditorSceneCommand.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

enum class ExternalChangeDecision { ReloadFromDisk, KeepEditing, SaveCopy,
                                    Cancel };

struct EditorDocumentIssue {
  SceneValueTarget target;
  std::string message;
};

// Owns the active authored scene JSON, its command history, and its
// conflict-safe persistence. Structural mutations are built with reusable
// scene-JSON helpers (EditorSceneJson), staged and validated through the
// shared scene validator, then recorded as reversible SceneCommands.
class EditorSceneDocument {
public:
  [[nodiscard]] bool open(const std::filesystem::path &path,
                          std::string &error);
  [[nodiscard]] bool reload(std::string &error);
  [[nodiscard]] bool save(std::string &error);
  [[nodiscard]] bool resolveExternalChange(
      ExternalChangeDecision decision, const std::filesystem::path &copyPath,
      std::string &error);

  [[nodiscard]] bool setValue(SceneValueTarget target, nlohmann::json value,
                              bool continuous, std::string &error);
  [[nodiscard]] bool removeValue(SceneValueTarget target, std::string &error);
  void endContinuousEdit() { continuousTarget_.reset(); }

  [[nodiscard]] bool createEntity(std::string &error);
  [[nodiscard]] bool deleteEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool reparent(std::string_view id,
                              std::optional<std::string> newParent,
                              std::string &error);
  [[nodiscard]] bool duplicateEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool addComponent(std::string_view id,
                                  std::string_view componentName,
                                  std::string &error);
  [[nodiscard]] bool removeComponent(std::string_view id,
                                     std::string_view componentName,
                                     std::string &error);

  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);

  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  [[nodiscard]] bool hasExternalConflict() const {
    return hasExternalConflict_;
  }
  [[nodiscard]] const std::optional<EditorDocumentIssue> &issue() const {
    return issue_;
  }
  [[nodiscard]] const std::string *issueFor(
      const SceneValueTarget &target) const;
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }
  [[nodiscard]] const nlohmann::json &json() const { return document_; }
  [[nodiscard]] const nlohmann::json *entity(std::string_view id) const;
  [[nodiscard]] const nlohmann::json *component(std::string_view entityId,
                                                std::string_view name) const;
  [[nodiscard]] std::string_view lastChangedEntityId() const {
    return lastChangedEntityId_;
  }

private:
  [[nodiscard]] nlohmann::json *value(const SceneValueTarget &target);
  [[nodiscard]] const nlohmann::json *
  value(const SceneValueTarget &target) const;
  [[nodiscard]] bool validate(const SceneValueTarget &target,
                              const nlohmann::json &replacement,
                              std::string &error) const;
  // Validates a staged document before touching the live document or history.
  [[nodiscard]] bool stageAndCommit(SceneCommand command, std::string &error);
  void reject(SceneValueTarget target, const std::string &error);
  void clearIssue() { issue_.reset(); }

  EditorDocumentStore store_;
  std::filesystem::path path_;
  FileRevision revision_;
  nlohmann::json document_;
  std::string savedCanonical_;
  std::vector<SceneCommand> undo_;
  std::vector<SceneCommand> redo_;
  std::optional<SceneValueTarget> continuousTarget_;
  std::string lastChangedEntityId_;
  std::optional<EditorDocumentIssue> issue_;
  bool hasExternalConflict_ = false;
};

} // namespace demi::editor
