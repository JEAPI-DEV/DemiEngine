#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace demi::editor {
class EditorWorkspace;
enum class EditorSourceKind {
  Scene2D,
  Scene3D,
  Hud,
  Prefab,
  Prefab2D,
  PrefabFromSelection,
  UiPrefab,
  Lua,
  Material,
  Terrain,
  TerrainFromSelection,
  Data,
  TerrainMaterial,
  TerrainMaterialSet,
  TerrainPalette
};
struct EditorSourceAssetOptions {
  std::string initialRole;
  std::string initialAsset;
  std::string initialPrefab;
};
struct EditorSourceAssetChoices {
  std::vector<std::string> materials;
  std::vector<std::string> models;
  std::vector<std::string> prefabs;
};
[[nodiscard]] EditorSourceAssetChoices
editorSourceAssetChoices(const EditorWorkspace &workspace);
bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error,
                        std::string_view selectedEntity = {},
                        std::filesystem::path destinationDirectory = {},
                        const EditorSourceAssetOptions &assetOptions = {});
} // namespace demi::editor
