#pragma once

#include "editor/EditorSceneDocument.h"

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/SceneLoader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

class EditorWorkspace {
public:
  [[nodiscard]] bool open(std::filesystem::path projectPath,
                          std::string &error);
  [[nodiscard]] bool refresh(std::string &error);
  [[nodiscard]] bool save(std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool editValue(SceneValueTarget target, nlohmann::json value,
                               bool continuous, std::string &error);
  [[nodiscard]] bool createEntity(std::string &error);
  [[nodiscard]] bool deleteEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool reparentEntity(std::string_view id,
                                    std::optional<std::string> newParent,
                                    std::string &error);
  [[nodiscard]] bool duplicateEntity(std::string_view id, std::string &error);
  [[nodiscard]] bool addComponent(std::string_view id,
                                  std::string_view componentName,
                                  std::string &error);
  [[nodiscard]] bool removeComponent(std::string_view id,
                                     std::string_view componentName,
                                     std::string &error);
  void endContinuousEdit() { sceneDocument_.endContinuousEdit(); }
  void refreshDiagnostics();

  [[nodiscard]] const runtime::LoadedProject &project() const {
    return *project_;
  }
  [[nodiscard]] runtime::LoadedProject &project() { return *project_; }
  [[nodiscard]] const std::filesystem::path &projectPath() const {
    return projectPath_;
  }
  [[nodiscard]] const std::vector<std::filesystem::path> &sources() const {
    return sources_;
  }
  [[nodiscard]] const Diagnostics &diagnostics() const { return diagnostics_; }
  [[nodiscard]] const EditorSceneDocument &sceneDocument() const {
    return sceneDocument_;
  }
  [[nodiscard]] EditorSceneDocument &sceneDocument() { return sceneDocument_; }

  void selectEntity(std::string id) { selectedEntityId_ = std::move(id); }
  [[nodiscard]] std::string_view selectedEntityId() const {
    return selectedEntityId_;
  }
  [[nodiscard]] const runtime::Entity *selectedEntity() const;

private:
  void discoverSources();
  void syncChangedEntity();
  [[nodiscard]] bool rebuildWorld(std::string &error);

  std::filesystem::path projectPath_;
  std::optional<runtime::LoadedProject> project_;
  EditorSceneDocument sceneDocument_;
  std::vector<std::filesystem::path> sources_;
  Diagnostics diagnostics_;
  std::string selectedEntityId_;
};

} // namespace demi::editor
