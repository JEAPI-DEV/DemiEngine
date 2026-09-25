#include "editor/EditorSourceCreation.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "editor/EditorDocumentStore.h"
#include "editor/EditorPrefabAuthoring.h"
#include "editor/EditorWorkspace.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace demi::editor {
bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error, std::string_view selectedEntity) {
  try {
    if (name.empty() || name.size() > 180 ||
        !std::ranges::all_of(name,
                             [](unsigned char c) {
                               return (c >= 'a' && c <= 'z') ||
                                      (c >= 'A' && c <= 'Z') ||
                                      (c >= '0' && c <= '9') || c == '_' ||
                                      c == '-' || c == '/';
                             }) ||
        name.front() == '/' || name.back() == '/') {
      error = "Use a name such as level_01 or chapter/level_01, without a file "
              "extension.";
      return false;
    }
    const bool scene =
        kind == EditorSourceKind::Scene2D || kind == EditorSourceKind::Scene3D;
    const bool asset =
        kind == EditorSourceKind::Material || kind == EditorSourceKind::Data;
    if (scene && workspace.hasUnsavedChanges()) {
      error = "Save or undo changes before creating a registered scene.";
      return false;
    }
    const std::string folder = kind == EditorSourceKind::Material
                                   ? "assets/materials"
                               : kind == EditorSourceKind::Data ? "assets/data"
                               : scene                          ? "scenes"
                               : kind == EditorSourceKind::Hud  ? "hud"
                               : kind == EditorSourceKind::Lua  ? "scripts"
                               : kind == EditorSourceKind::UiPrefab ? "ui"
                                                                    : "prefabs";
    const std::string suffix =
        kind == EditorSourceKind::Material   ? ".material.json"
        : kind == EditorSourceKind::Data     ? ".json"
        : scene                              ? ".scene.json"
        : kind == EditorSourceKind::Hud      ? ".hud.json"
        : kind == EditorSourceKind::Lua      ? ".lua"
        : kind == EditorSourceKind::UiPrefab ? ".ui.prefab.json"
                                             : ".prefab.json";
    const auto root = std::filesystem::weakly_canonical(
        workspace.project().project.projectDirectory);
    const auto relative =
        asset ? std::filesystem::path(folder) / name /
                    (std::filesystem::path(name).filename().string() + suffix)
              : std::filesystem::path(folder) / (name + suffix);
    const auto target = std::filesystem::weakly_canonical(root / relative);
    const auto within = target.lexically_relative(root);
    if (within.empty() || within.is_absolute() || *within.begin() == "..") {
      error = "Source path leaves the project.";
      return false;
    }
    nlohmann::json document{{"format_version", 1}};
    const std::string assetId =
        std::string("asset://") +
        (kind == EditorSourceKind::Material ? "materials/" : "data/") + name;
    if (asset) {
      const auto registry = loadAssetRegistry(root);
      const auto manifest =
          target.parent_path() / (target.stem().string() + ".asset.json");
      if (findAsset(registry, assetId) || std::filesystem::exists(manifest)) {
        error = "This asset already exists; choose another name.";
        return false;
      }
    }
    std::string content;
    if (kind == EditorSourceKind::PrefabFromSelection) {
      const auto prefab =
          makeEntityPrefab(workspace.sceneDocument().json(), selectedEntity,
                           "prefab://" + name, error);
      if (!prefab)
        return false;
      document = *prefab;
    } else if (kind == EditorSourceKind::Material) {
      document["shader"] = "builtin://lit";
      document["parameters"] = {{"base_color", {1, 1, 1, 1}}};
    } else if (kind == EditorSourceKind::Data) {
      // A versioned empty data document is editable without a custom schema.
    } else if (kind == EditorSourceKind::Lua)
      content = "---@demi_component\nlocal Behaviour = {}\n\nfunction "
                "Behaviour:on_start()\nend\n\nfunction "
                "Behaviour:on_update(dt)\nend\n\nreturn Behaviour\n";
    else if (kind == EditorSourceKind::Hud ||
             kind == EditorSourceKind::UiPrefab) {
      document["root"] = {{"id", "root"},
                          {"type", "container"},
                          {"anchor_min", {0, 0}},
                          {"anchor_max", {1, 1}},
                          {"children", nlohmann::json::array()}};
      if (kind == EditorSourceKind::UiPrefab)
        document["id"] = "ui-prefab://" + name;
      else
        document["canvas_size"] = {960, 540};
    } else {
      document["id"] = (scene ? "scene://" : "prefab://") + name;
      document["name"] = name;
      document["entities"] = nlohmann::json::array();
      if (kind == EditorSourceKind::Scene3D) {
        document["entities"].push_back(
            {{"id", "camera"},
             {"components",
              {{"Transform3D", {{"position", {0, 3, 6}}}},
               {"Camera3D", {{"target_offset", {0, -3, -6}}}}}}});
        document["entities"].push_back(
            {{"id", "sun"},
             {"components", {{"DirectionalLight", nlohmann::json::object()}}}});
      } else if (kind == EditorSourceKind::Scene2D)
        document["entities"].push_back(
            {{"id", "camera"},
             {"components",
              {{"Transform2D", nlohmann::json::object()},
               {"Camera2D", nlohmann::json::object()}}}});
      else if (kind == EditorSourceKind::Prefab2D)
        document["entities"].push_back(
            {{"id", "body"},
             {"components", {{"Transform2D", nlohmann::json::object()}}}});
      else
        document["entities"].push_back(
            {{"id", "body"},
             {"components", {{"Transform3D", nlohmann::json::object()}}}});
    }
    if (content.empty())
      content = document.dump(2) + "\n";
    EditorDocumentStore store;
    std::filesystem::create_directories(target.parent_path());
    if (!store.writeNew(target, content, error))
      return false;
    created = target;
    if (asset) {
      const auto imported = assets::importAsset(
          {.projectDirectory = root,
           .source = target,
           .id = assetId,
           .type =
               kind == EditorSourceKind::Material ? "Material" : "DataAsset"});
      if (hasErrors(imported.diagnostics)) {
        error = "Created " + target.string() + "; asset registration failed: " +
                imported.diagnostics.front().message;
        workspace.refreshAssetMetadata();
        return false;
      }
      created = imported.manifestPath;
    }
    if (scene &&
        (!workspace.addProjectScene("scene://" + name, relative, error) ||
         !workspace.saveProject(error))) {
      error = "Created " + target.string() +
              ", but scene registration failed: " + error;
      workspace.refreshAssetMetadata();
      return false;
    }
    workspace.refreshAssetMetadata();
    return true;
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}
} // namespace demi::editor
