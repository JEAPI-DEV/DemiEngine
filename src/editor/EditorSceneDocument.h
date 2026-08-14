#pragma once

#include "editor/EditorDocumentStore.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

struct SceneValueTarget {
  std::string entityId;
  std::string component;
  std::string field;

  friend bool operator==(const SceneValueTarget &,
                         const SceneValueTarget &) = default;
};

class EditorSceneDocument {
public:
  [[nodiscard]] bool open(const std::filesystem::path &path,
                          std::string &error);
  [[nodiscard]] bool reload(std::string &error);
  [[nodiscard]] bool save(std::string &error);

  [[nodiscard]] bool setValue(SceneValueTarget target, nlohmann::json value,
                              bool continuous, std::string &error);
  void endContinuousEdit() { continuousTarget_.reset(); }
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);

  [[nodiscard]] bool isDirty() const;
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }
  [[nodiscard]] const nlohmann::json &json() const { return document_; }
  [[nodiscard]] const nlohmann::json *entity(std::string_view id) const;
  [[nodiscard]] const nlohmann::json *component(std::string_view entityId,
                                                std::string_view name) const;
  [[nodiscard]] std::string_view lastChangedEntityId() const {
    return lastChangedEntityId_;
  }

private:
  struct SetValueCommand {
    SceneValueTarget target;
    nlohmann::json before;
    nlohmann::json after;
  };

  [[nodiscard]] nlohmann::json *value(const SceneValueTarget &target);
  [[nodiscard]] const nlohmann::json *
  value(const SceneValueTarget &target) const;
  [[nodiscard]] bool validate(const SceneValueTarget &target,
                              const nlohmann::json &replacement,
                              std::string &error) const;
  void apply(const SetValueCommand &command, bool forward);

  EditorDocumentStore store_;
  std::filesystem::path path_;
  FileRevision revision_;
  nlohmann::json document_;
  std::string savedCanonical_;
  std::vector<SetValueCommand> undo_;
  std::vector<SetValueCommand> redo_;
  std::optional<SceneValueTarget> continuousTarget_;
  std::string lastChangedEntityId_;
};

} // namespace demi::editor
