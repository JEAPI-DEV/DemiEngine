#pragma once

#include "editor/EditorJsonDocument.h"

#include "demi/runtime/ui/UiModel.h"

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

  EditorJsonDocument document_;
  runtime::ui::UiDocument preview_;
};

} // namespace demi::editor
