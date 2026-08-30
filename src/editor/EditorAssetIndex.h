#pragma once

#include "demi/assets/AssetGroup.h"
#include "demi/assets/AssetRegistry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::editor {

enum class EditorAssetCookState { Uncooked, Current, Stale };

struct EditorAssetRecord {
  AssetManifest manifest;
  EditorAssetCookState cookState = EditorAssetCookState::Uncooked;
  std::string cookReason;
  Diagnostics diagnostics;
};

// Read-only projection over authored manifests, group manifests, registry
// diagnostics, and the last Linux cook. It never becomes an editor-only asset
// database.
class EditorAssetIndex {
public:
  void refresh(const std::filesystem::path &projectDirectory,
               const std::vector<std::filesystem::path> &sources);

  [[nodiscard]] const std::vector<EditorAssetRecord> &assets() const {
    return assets_;
  }
  [[nodiscard]] const std::vector<assets::AssetGroupDescriptor> &
  groups() const {
    return groups_;
  }
  [[nodiscard]] const Diagnostics &diagnostics() const { return diagnostics_; }
  [[nodiscard]] const AssetRegistry &registry() const { return registry_; }
  [[nodiscard]] const EditorAssetRecord *
  findByManifest(const std::filesystem::path &path) const;
  [[nodiscard]] const EditorAssetRecord *
  findBySource(const std::filesystem::path &path) const;
  [[nodiscard]] std::vector<std::string> types() const;

private:
  std::vector<EditorAssetRecord> assets_;
  std::vector<assets::AssetGroupDescriptor> groups_;
  Diagnostics diagnostics_;
  AssetRegistry registry_;
};

[[nodiscard]] const char *editorAssetCookStateName(EditorAssetCookState state);

} // namespace demi::editor
