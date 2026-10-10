#pragma once

#include "editor/EditorJsonDocument.h"

#include "demi/runtime/ui/UiModel.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

// Owns authored HUD history and translates stable UI-node operations into the
// nested JSON document. The runtime UiDocument is a rebuildable preview only.
class EditorHudDocument {
public:
  [[nodiscard]] bool open(std::filesystem::path path, std::string &error);
  [[nodiscard]] bool createNode(std::string_view type,
                                std::string_view parentId,
                                std::string &createdId, std::string &error,
                                std::optional<runtime::Vec2> position = std::nullopt);
  [[nodiscard]] bool createPrefabInstance(std::string_view prefabReference,
                                          std::string_view parentId,
                                          std::string &createdId,
                                          std::string &error,
                                          std::optional<runtime::Vec2> position = std::nullopt);
  [[nodiscard]] bool unpackPrefab(std::string_view id, std::string &error);
  [[nodiscard]] bool reparentNode(std::string_view id,
                                  std::string_view parentId,
                                  std::string &error);
  [[nodiscard]] bool duplicateNode(std::string_view id,
                                   std::string &createdId,
                                   std::string &error);
  [[nodiscard]] bool deleteNode(std::string_view id, std::string &error);
  // Copy only authored topmost controls, never the document root or generated
  // UI-prefab children. Each multi-element mutation is one undoable command.
  [[nodiscard]] std::optional<nlohmann::json>
  exportNodes(std::span<const std::string> ids, std::string &error) const;
  [[nodiscard]] bool pasteNodes(const nlohmann::json &payload,
                                std::string_view parentId,
                                std::vector<std::string> &createdIds,
                                std::string &error);
  [[nodiscard]] bool duplicateNodes(std::span<const std::string> ids,
                                    std::vector<std::string> &createdIds,
                                    std::string &error);
  [[nodiscard]] bool deleteNodes(std::span<const std::string> ids,
                                 std::string &error);
  [[nodiscard]] bool setCanvasSize(runtime::Vec2 size, std::string &error);
  [[nodiscard]] bool setNodeField(std::string_view id, std::string_view field,
                                  nlohmann::json value, std::string &error,
                                  bool continuous = false);
  void endContinuousEdit() { document_.endContinuousEdit(); }
  [[nodiscard]] bool setNodeAnchors(std::string_view id,
                                    runtime::Vec2 anchorMin,
                                    runtime::Vec2 anchorMax,
                                    std::string &error);
  [[nodiscard]] bool previewNodeAction(std::string_view id);
  [[nodiscard]] bool resetPreviewState(std::string &error);
  [[nodiscard]] bool hasPreviewState() const {
    return !previewVisibility_.empty();
  }
  [[nodiscard]] bool resetPrefabTarget(std::string_view instance,
                                       std::string_view target,
                                       std::string &error);
  [[nodiscard]] bool resetNodeOverride(std::string_view id,
                                       std::string_view field,
                                       std::string &error);
  [[nodiscard]] const runtime::ui::UiPrefabNodeOrigin *
  prefabOrigin(std::string_view id) const;
  [[nodiscard]] const nlohmann::json *
  authoringProperties(std::string_view id) const;
  [[nodiscard]] const nlohmann::json *effectiveNode(std::string_view id) const;
  [[nodiscard]] const nlohmann::json *nodeOverrides(std::string_view id) const;
  [[nodiscard]] std::optional<nlohmann::json>
  prefabArguments(std::string_view id, std::string &error) const;
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool save(std::string &error) { return document_.save(error); }
  [[nodiscard]] bool restore(nlohmann::json document, std::string &error);

  [[nodiscard]] bool isDirty() const { return document_.isDirty(); }
  [[nodiscard]] bool canUndo() const { return document_.canUndo(); }
  [[nodiscard]] bool canRedo() const { return document_.canRedo(); }
  [[nodiscard]] const std::filesystem::path &path() const {
    return document_.path();
  }
  [[nodiscard]] const runtime::ui::UiDocument &preview() const {
    return preview_;
  }
  [[nodiscard]] const nlohmann::json &json() const { return document_.json(); }
  [[nodiscard]] bool hasImplicitRoot() const;
  [[nodiscard]] const nlohmann::json *authoredNode(std::string_view id) const;

private:
  [[nodiscard]] bool rebuild(std::string &error);
  [[nodiscard]] bool replaceAndRebuild(nlohmann::json replacement,
                                       std::string &error,
                                       std::string_view continuousKey = {});

  [[nodiscard]] nlohmann::json *mutableNodeProperties(nlohmann::json &document,
                                                      std::string_view id);
  void discardOverridesForSource(nlohmann::json &document,
                                 std::string_view pointer,
                                 bool deleting = false) const;
  void pruneOverrides(nlohmann::json &document, std::string_view id) const;
  [[nodiscard]] nlohmann::json *mutableAuthoredNode(nlohmann::json &document,
                                                    std::string_view id) const;
  [[nodiscard]] nlohmann::json *childStorage(nlohmann::json &document,
                                             std::string_view parent,
                                             std::string &prefix);
  [[nodiscard]] std::string newChildId(std::string_view base,
                                       std::string_view prefix) const;
  void applyPreviewState();
  std::unordered_map<std::string, bool> previewVisibility_;
  EditorJsonDocument document_;
  runtime::ui::UiPrefabExpansionResult composition_;
  runtime::ui::UiDocument preview_;
};

} // namespace demi::editor
