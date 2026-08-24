#pragma once

#include "editor/EditorJsonDocument.h"

#include "demi/runtime/ui/UiModel.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace demi::editor {

// Owns authored HUD history and translates stable UI-node operations into the
// nested JSON document. The runtime UiDocument is a rebuildable preview only.
class EditorHudDocument {
public:
  [[nodiscard]] bool open(std::filesystem::path path, std::string &error);
  [[nodiscard]] bool createNode(std::string_view type,
                                std::string_view parentId,
                                std::string &createdId, std::string &error);
  [[nodiscard]] bool deleteNode(std::string_view id, std::string &error);
  [[nodiscard]] bool setNodeField(std::string_view id, std::string_view field,
                                  nlohmann::json value, std::string &error);
  [[nodiscard]] bool undo(std::string &error);
  [[nodiscard]] bool redo(std::string &error);
  [[nodiscard]] bool save(std::string &error) { return document_.save(error); }

  [[nodiscard]] bool isDirty() const { return document_.isDirty(); }
  [[nodiscard]] bool canUndo() const { return document_.canUndo(); }
  [[nodiscard]] bool canRedo() const { return document_.canRedo(); }
  [[nodiscard]] const std::filesystem::path &path() const {
    return document_.path();
  }
  [[nodiscard]] const runtime::ui::UiDocument &preview() const {
    return preview_;
  }
  [[nodiscard]] const nlohmann::json *authoredNode(std::string_view id) const;

private:
  [[nodiscard]] bool rebuild(std::string &error);

  EditorJsonDocument document_;
  runtime::ui::UiDocument preview_;
};

} // namespace demi::editor
