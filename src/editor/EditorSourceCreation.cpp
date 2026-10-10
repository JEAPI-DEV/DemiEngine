#include "editor/EditorSourceCreation.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataAssetContent.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/assets/RenderAsset.h"
#include "editor/EditorDocumentStore.h"
#include "editor/EditorPrefabAuthoring.h"
#include "editor/EditorSpecializedDocument.h"
#include "editor/EditorTerrainRuntime.h"
#include "editor/EditorWorkspace.h"
#include <algorithm>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::editor {
namespace {
bool isTypedDataAsset(const EditorSourceKind kind) {
  return kind == EditorSourceKind::TerrainMaterial ||
         kind == EditorSourceKind::TerrainMaterialSet ||
         kind == EditorSourceKind::TerrainPalette;
}

std::string_view contentType(const EditorSourceKind kind) {
  switch (kind) {
  case EditorSourceKind::TerrainMaterial:
    return "terrain_material";
  case EditorSourceKind::TerrainMaterialSet:
    return "terrain_material_set";
  case EditorSourceKind::TerrainPalette:
    return "terrain_palette";
  default:
    return {};
  }
}

bool validateTypedSource(const EditorSourceKind kind,
                         const nlohmann::json &value,
                         const AssetRegistry &registry, const std::string &id,
                         const std::filesystem::path &path,
                         std::string &error) {
  const auto parsed = assets::parseDataDocument(value.dump(), path);
  if (!parsed.document || hasErrors(parsed.diagnostics)) {
    error = parsed.diagnostics.empty() ? "Could not parse the new data asset."
                                       : parsed.diagnostics.front().message;
    return false;
  }
  AssetManifest proposed;
  proposed.id = id;
  proposed.type = "DataAsset";
  proposed.sourcePath = path;
  proposed.manifestPath =
      path.parent_path() / (path.stem().string() + ".asset.json");
  proposed.settingsJson =
      nlohmann::json{{"content_type", std::string(contentType(kind))}}.dump();
  const Diagnostics diagnostics =
      assets::validateDataAssetDocument(proposed, *parsed.document, registry);
  if (hasErrors(diagnostics)) {
    error = diagnostics.front().message;
    return false;
  }
  const auto inspected = assets::inspectDataAssetContent(
      contentType(kind), *parsed.document, registry, id);
  if (hasErrors(inspected.diagnostics)) {
    error = inspected.diagnostics.front().message;
    return false;
  }
  return true;
}
} // namespace

EditorSourceAssetChoices
editorSourceAssetChoices(const EditorWorkspace &workspace) {
  EditorSourceAssetChoices choices;
  const auto root = workspace.project().project.projectDirectory;
  const auto registry = loadAssetRegistry(root);
  for (const AssetManifest &manifest : registry.assets) {
    if (manifest.type == "Material") {
      try {
        if (assets::loadMaterialAsset(manifest.sourcePath))
          choices.materials.push_back(manifest.id);
      } catch (const std::exception &) {
        // Keep invalid materials in diagnostics rather than creation choices.
      }
    }
    if (manifest.type == "Model3D")
      choices.models.push_back(manifest.id);
    if (manifest.type != "DataAsset")
      continue;
    const auto metadata = assets::dataAssetMetadata(manifest);
    if (!metadata || metadata->contentType != "terrain_material")
      continue;
    try {
      if (assets::loadTerrainMaterialAsset(registry, manifest.id))
        choices.materials.push_back(manifest.id);
    } catch (const std::exception &) {
      // Invalid sources are reported by project diagnostics, not offered here.
    }
  }
  const auto prefabRoot = root / "prefabs";
  for (const auto &source : workspace.sources()) {
    const auto relative = source.lexically_relative(prefabRoot);
    const std::string filename = source.filename().string();
    if (relative.empty() || relative.is_absolute() ||
        *relative.begin() == ".." || !filename.ends_with(".prefab.json") ||
        filename.ends_with(".ui.prefab.json"))
      continue;
    std::ifstream input(source);
    if (!input)
      continue;
    const auto document = nlohmann::json::parse(input, nullptr, false);
    std::string name = relative.generic_string();
    name.resize(name.size() - std::string_view(".prefab.json").size());
    const std::string id = "prefab://" + name;
    try {
      if (!document.is_object() || !document.contains("id") ||
          !document["id"].is_string() || document["id"] != id ||
          hasErrors(validateSpecializedDocument(EditorSpecializedKind::Prefab,
                                                source, document)))
        continue;
    } catch (const std::exception &) {
      continue;
    }
    choices.prefabs.push_back(id);
  }
  std::ranges::sort(choices.materials);
  std::ranges::sort(choices.models);
  std::ranges::sort(choices.prefabs);
  return choices;
}

EditorSourceDescription editorSourceDescription(const EditorSourceKind kind) {
  switch (kind) {
  case EditorSourceKind::Scene2D: return {"2D Scene", "scenes", ".scene.json"};
  case EditorSourceKind::Scene3D: return {"3D Scene", "scenes", ".scene.json"};
  case EditorSourceKind::Hud: return {"HUD", "hud", ".hud.json"};
  case EditorSourceKind::Prefab2D: return {"2D Prefab", "prefabs", ".prefab.json"};
  case EditorSourceKind::Prefab: return {"3D Prefab", "prefabs", ".prefab.json"};
  case EditorSourceKind::PrefabFromSelection: return {"Prefab", "prefabs", ".prefab.json"};
  case EditorSourceKind::UiPrefab: return {"UI Prefab", "ui", ".ui.prefab.json"};
  case EditorSourceKind::Lua: return {"Lua Behaviour", "scripts", ".lua"};
  case EditorSourceKind::Material: return {"Material", "assets/materials", ".material.json", true};
  case EditorSourceKind::Terrain:
  case EditorSourceKind::TerrainFromSelection: return {"Terrain", "assets/terrains", ".terrain.json", true};
  case EditorSourceKind::Data: return {"Data Asset", "assets/data", ".json", true};
  case EditorSourceKind::TerrainMaterial: return {"Terrain Material", "assets/terrain_materials", ".json", true};
  case EditorSourceKind::TerrainMaterialSet: return {"Terrain Material Set", "assets/terrain_material_sets", ".json", true};
  case EditorSourceKind::TerrainPalette: return {"Terrain Palette", "assets/terrain_palettes", ".json", true};
  }
  return {};
}

std::filesystem::path editorSourceDirectory(
    EditorSourceKind kind, const std::filesystem::path &selectedFolder) {
  const auto description = editorSourceDescription(kind);
  const auto relative = selectedFolder.lexically_normal().lexically_relative(description.folder);
  if (!selectedFolder.is_absolute() && !relative.empty() &&
      *relative.begin() != "..")
    return selectedFolder;
  return std::filesystem::path(description.folder);
}

std::filesystem::path editorSourceRelativePath(
    EditorSourceKind kind, std::string_view name, const std::filesystem::path &directory) {
  const auto description = editorSourceDescription(kind);
  const auto folder = directory.empty() ? std::filesystem::path(description.folder) : directory;
  const auto filename = std::string(name) + std::string(description.suffix);
  return description.assetDirectory
      ? folder / name / (std::filesystem::path(name).filename().string() + std::string(description.suffix))
      : folder / filename;
}

bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error, std::string_view selectedEntity,
                        std::filesystem::path destinationDirectory,
                        const EditorSourceAssetOptions &assetOptions) {
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
    const bool terrain = kind == EditorSourceKind::Terrain ||
                         kind == EditorSourceKind::TerrainFromSelection;
    const bool asset = kind == EditorSourceKind::Material ||
                       kind == EditorSourceKind::Data || terrain ||
                       isTypedDataAsset(kind);
    if (scene && workspace.hasUnsavedChanges()) {
      error = "Save or undo changes before creating a registered scene.";
      return false;
    }
    const auto description = editorSourceDescription(kind);
    const std::string folder(description.folder);
    const auto root = std::filesystem::weakly_canonical(
        workspace.project().project.projectDirectory);
    std::filesystem::path sourceDirectory(folder);
    std::filesystem::path qualifiedName(name);
    if (!destinationDirectory.empty()) {
      destinationDirectory = destinationDirectory.lexically_normal();
      const auto withinSourceDirectory =
          destinationDirectory.lexically_relative(sourceDirectory);
      if (destinationDirectory.is_absolute() ||
          withinSourceDirectory.empty() ||
          withinSourceDirectory.is_absolute() ||
          *withinSourceDirectory.begin() == "..") {
        error = "The destination must stay inside the source type's authored "
                "folder.";
        return false;
      }
      sourceDirectory = destinationDirectory;
      if (withinSourceDirectory != ".")
        qualifiedName = withinSourceDirectory / qualifiedName;
    }
    const auto relative = editorSourceRelativePath(kind, name, sourceDirectory);
    const auto target = std::filesystem::weakly_canonical(root / relative);
    const auto within = target.lexically_relative(root);
    if (within.empty() || within.is_absolute() || *within.begin() == "..") {
      error = "Source path leaves the project.";
      return false;
    }
    std::error_code existenceError;
    if (std::filesystem::exists(target, existenceError)) {
      error = "A source with this name already exists; choose another name.";
      return false;
    }
    if (existenceError) {
      error = "Could not inspect the source destination: " +
              existenceError.message();
      return false;
    }
    nlohmann::json document{{"format_version", 1}};
    const std::string assetId =
        std::string("asset://") +
        (kind == EditorSourceKind::TerrainMaterial ? "terrain_materials/"
         : kind == EditorSourceKind::TerrainMaterialSet
             ? "terrain_material_sets/"
         : kind == EditorSourceKind::TerrainPalette ? "terrain_palettes/"
         : kind == EditorSourceKind::Material       ? "materials/"
         : terrain                                  ? "terrains/"
                                                    : "data/") +
        qualifiedName.generic_string();
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
                           "prefab://" + qualifiedName.generic_string(), error);
      if (!prefab)
        return false;
      document = *prefab;
    } else if (kind == EditorSourceKind::Material) {
      document["shader"] = "builtin://lit";
      document["parameters"] = {{"base_color", {1, 1, 1, 1}}};
    } else if (terrain) {
      document["id"] = assetId;
      document["name"] = std::filesystem::path(name).filename().string();
      if (kind == EditorSourceKind::TerrainFromSelection) {
        const auto *component =
            workspace.sceneDocument().component(selectedEntity, "Terrain3D");
        if (!component || !component->contains("recipe")) {
          error =
              "Choose an authored terrain with an inline procedural recipe.";
          return false;
        }
        document["recipe"] = component->at("recipe");
      } else {
        document["recipe"] = defaultEditorTerrainRecipe();
      }
    } else if (isTypedDataAsset(kind)) {
      document["name"] = std::filesystem::path(name).filename().string();
      if (kind == EditorSourceKind::TerrainMaterial) {
        document["base_color"] = {0.8, 0.8, 0.8, 1.0};
      } else {
        const std::string role =
            assetOptions.initialRole.empty()
                ? kind == EditorSourceKind::TerrainMaterialSet ? "ground"
                                                               : "soil"
                : assetOptions.initialRole;
        if (assetOptions.initialAsset.empty()) {
          error = "Choose an existing asset for the first role.";
          return false;
        }
        const auto choices = editorSourceAssetChoices(workspace);
        if (kind == EditorSourceKind::TerrainMaterialSet) {
          if (std::ranges::find(choices.materials, assetOptions.initialAsset) ==
              choices.materials.end()) {
            error = "Choose an existing terrain material for the ground role.";
            return false;
          }
          document["roles"] = {{role, assetOptions.initialAsset}};
        } else {
          const bool isModel =
              std::ranges::find(choices.models, assetOptions.initialAsset) !=
              choices.models.end();
          const bool isMaterial =
              std::ranges::find(choices.materials, assetOptions.initialAsset) !=
              choices.materials.end();
          if (!isModel && (!isMaterial || assetOptions.initialPrefab.empty())) {
            error = "Choose a Model3D asset, or a terrain material with an "
                    "entity prefab.";
            return false;
          }
          nlohmann::json entry{{"asset", assetOptions.initialAsset}};
          if (!assetOptions.initialPrefab.empty()) {
            if (!assetOptions.initialPrefab.starts_with("prefab://") ||
                std::ranges::find(choices.prefabs,
                                  assetOptions.initialPrefab) ==
                    choices.prefabs.end()) {
              error = "Choose an existing entity prefab for the first role.";
              return false;
            }
            entry["prefab"] = assetOptions.initialPrefab;
          }
          document["roles"] = {{role, std::move(entry)}};
        }
      }
      const auto registry = loadAssetRegistry(root);
      if (!validateTypedSource(kind, document, registry, assetId, target,
                               error)) {
        if (error.empty())
          error = "The initial data asset is invalid.";
        return false;
      }
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
        document["id"] = "ui-prefab://" + qualifiedName.generic_string();
      else
        document["canvas_size"] = {960, 540};
    } else {
      document["id"] =
          (scene ? "scene://" : "prefab://") + qualifiedName.generic_string();
      document["name"] = qualifiedName.generic_string();
      document["entities"] = nlohmann::json::array();
      if (kind == EditorSourceKind::Scene3D) {
        document["entities"].push_back(
            {{"id", "camera"},
             {"components",
              {{"Transform3D", {{"position", {0, 3, 6}}}},
               {"Camera3D", {{"target_offset", {0, -3, -6}}}}}}});
        document["entities"].push_back(
            {{"id", "sun"},
             {"name", "Basic Lighting"},
             {"components", {{"Transform3D", {{"position", {0, 5, 0}}}},
                              {"Environment3D", nlohmann::json::object()},
                              {"DirectionalLight", {{"casts_shadows", true}}}}}});
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
    if (kind == EditorSourceKind::PrefabFromSelection &&
        assetOptions.replaceSelectionWithPrefab) {
      const auto sourceId =
          document.at("entities").front().at("id").get<std::string>();
      if (!workspace.replaceHierarchyWithPrefab(sourceId, target, error)) {
        // Roll back only the source we just created; never remove a concurrent
        // edit.
        std::string current;
        FileRevision revision;
        std::string readError;
        std::error_code filesystemError;
        if (!std::filesystem::is_symlink(target, filesystemError) &&
            !filesystemError &&
            store.read(target, current, revision, readError) &&
            current == content &&
            std::filesystem::remove(target, filesystemError) &&
            !filesystemError)
          created.clear();
        else
          error += " The created prefab remains at " + target.string() +
                   "; inspect it before retrying.";
        workspace.refreshAssetMetadata();
        return false;
      }
    }
    if (asset) {
      const auto imported = assets::importAsset(
          {.projectDirectory = root,
           .source = target,
           .id = assetId,
           .type = kind == EditorSourceKind::Material ? "Material"
                   : terrain                          ? "Terrain"
                                                      : "DataAsset",
           .dataContentType =
               isTypedDataAsset(kind)
                   ? std::optional<std::string>(std::string(contentType(kind)))
                   : std::nullopt});
      if (hasErrors(imported.diagnostics)) {
        error = "Asset registration failed: " +
                imported.diagnostics.front().message;
        if (isTypedDataAsset(kind)) {
          std::string currentContent;
          FileRevision revision;
          std::string readError;
          std::error_code filesystemError;
          const bool isOwnSource =
              !std::filesystem::is_symlink(target, filesystemError) &&
              !filesystemError &&
              store.read(target, currentContent, revision, readError) &&
              currentContent == content;
          if (isOwnSource && std::filesystem::remove(target, filesystemError) &&
              !filesystemError) {
            created.clear();
          } else {
            error += " The authored source remains at " + target.string() +
                     "; inspect it before retrying.";
          }
        } else {
          error = "Created " + target.string() + "; " + error;
        }
        workspace.refreshAssetMetadata();
        return false;
      }
      created = imported.manifestPath;
      if (kind == EditorSourceKind::TerrainFromSelection) {
        workspace.refreshAssetMetadata();
        if (!workspace.assignTerrainAsset(selectedEntity, assetId, error)) {
          error = "Created " + target.string() +
                  ", but the scene reference was not changed: " + error;
          return false;
        }
      }
    }
    if (scene &&
        (!workspace.addProjectScene("scene://" + qualifiedName.generic_string(),
                                    relative, error) ||
         !workspace.saveProject(error))) {
      error = "Created " + target.string() +
              ", but scene registration failed: " + error;
      workspace.refreshAssetMetadata();
      return false;
    }
    if (target.extension() == ".lua")
      workspace.notifyScriptCreated(target);
    else
      workspace.refreshAssetMetadata();
    return true;
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}
} // namespace demi::editor
