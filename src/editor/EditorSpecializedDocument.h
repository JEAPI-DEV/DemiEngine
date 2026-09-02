#pragma once

#include "editor/EditorAssetIndex.h"
#include "editor/EditorJsonDocument.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace demi::editor {

enum class EditorSpecializedKind {
  Prefab,
  Hud,
  Material,
  Animation,
  Data,
  Audio
};

class EditorSpecializedDocument {
public:
  [[nodiscard]] bool open(const std::filesystem::path &selectedSource,
                          const EditorAssetIndex &assets, std::string &error);

  [[nodiscard]] EditorSpecializedKind kind() const { return kind_; }
  [[nodiscard]] std::string_view title() const;
  [[nodiscard]] EditorJsonDocument &document() { return document_; }
  [[nodiscard]] const EditorJsonDocument &document() const { return document_; }
  [[nodiscard]] const nlohmann::json &expandedPrefab() const {
    return expandedPrefab_;
  }
  [[nodiscard]] const nlohmann::json &prefabDiff() const { return prefabDiff_; }
  [[nodiscard]] const std::optional<std::filesystem::path> &
  associatedManifest() const {
    return associatedManifest_;
  }
  void rebuildPreview();
  [[nodiscard]] bool applyPrefabOverrides(std::size_t instanceIndex,
                                          std::string &error);
  [[nodiscard]] bool revertPrefabOverrides(std::size_t instanceIndex,
                                           std::string &error);

private:
  EditorJsonDocument document_;
  EditorSpecializedKind kind_ = EditorSpecializedKind::Data;
  nlohmann::json expandedPrefab_ = nlohmann::json::object();
  nlohmann::json prefabDiff_ = nlohmann::json::array();
  std::optional<std::filesystem::path> associatedManifest_;
};

[[nodiscard]] Diagnostics
validateSpecializedDocument(EditorSpecializedKind kind,
                            const std::filesystem::path &path,
                            const nlohmann::json &document);

} // namespace demi::editor
