#pragma once

#include "editor/EditorJsonDocument.h"

#include "demi/assets/AssetRegistry.h"

#include <filesystem>
#include <string>

namespace demi::editor {

// Owns the authored terrain source. Preview meshes and cached heightfields are
// deliberately outside this document and never enter its undo or save data.
class EditorTerrainAssetDocument {
public:
  [[nodiscard]] bool open(const AssetManifest &manifest, std::string &error);
  [[nodiscard]] bool setRecipe(nlohmann::json recipe, std::string &error);
  [[nodiscard]] bool restore(nlohmann::json source, std::string &error);
  [[nodiscard]] bool undo(std::string &error) { return source_.undo(error); }
  [[nodiscard]] bool redo(std::string &error) { return source_.redo(error); }
  [[nodiscard]] bool save(std::string &error);

  [[nodiscard]] const nlohmann::json &json() const { return source_.json(); }
  [[nodiscard]] const nlohmann::json &recipe() const {
    return source_.json().at("recipe");
  }
  [[nodiscard]] const std::filesystem::path &path() const {
    return source_.path();
  }
  [[nodiscard]] const std::filesystem::path &manifestPath() const {
    return manifestPath_;
  }
  [[nodiscard]] const std::string &id() const { return id_; }
  [[nodiscard]] bool isDirty() const { return source_.isDirty(); }
  [[nodiscard]] bool canUndo() const { return source_.canUndo(); }
  [[nodiscard]] bool canRedo() const { return source_.canRedo(); }

private:
  EditorJsonDocument source_;
  std::filesystem::path manifestPath_;
  std::string id_;
};

} // namespace demi::editor
