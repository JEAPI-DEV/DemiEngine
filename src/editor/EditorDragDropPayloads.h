#pragma once

namespace demi::editor {

// Shared payload names keep panel-to-panel authoring interactions compatible.
inline constexpr char EditorSceneEntityPayload[] = "DEMI_SCENE_ENTITY";
inline constexpr char EditorPrefabSourcePayload[] = "DEMI_PREFAB_SOURCE";
inline constexpr char EditorTerrainAssetPayload[] = "DEMI_TERRAIN_ASSET";
// Null-terminated stable EditorModule::id; consumers resolve it against the
// current catalog on delivery.
inline constexpr char EditorModulePayload[] = "DEMI_EDITOR_MODULE";

} // namespace demi::editor
