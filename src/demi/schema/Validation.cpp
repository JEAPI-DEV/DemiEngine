#include "demi/schema/Validation.h"

#include "demi/assets/AssetGroup.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/SceneBudget3D.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/packages/PackageManifest.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace demi {

namespace {

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void requireToken(Diagnostics &diagnostics, const std::string &text,
                  const std::filesystem::path &path, const char *token,
                  const char *code, const char *message,
                  const char *suggestion) {
  if (text.find(token) != std::string::npos) {
    return;
  }

  diagnostics.push_back(Diagnostic{
      .severity = Severity::Error,
      .code = code,
      .message = message,
      .path = path.string(),
      .suggestion = suggestion,
  });
}

std::optional<std::filesystem::path>
findProjectDirectory(const std::filesystem::path &path) {
  std::filesystem::path cursor =
      std::filesystem::is_regular_file(path) ? path.parent_path() : path;
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "demi.project.json")) {
      return cursor;
    }

    bool hasProjectFile = false;
    if (std::filesystem::exists(cursor)) {
      for (const std::filesystem::directory_entry &entry :
           std::filesystem::directory_iterator(cursor)) {
        if (entry.is_regular_file() && isProjectFile(entry.path())) {
          hasProjectFile = true;
          break;
        }
      }
    }
    if (hasProjectFile) {
      return cursor;
    }

    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }

  return std::nullopt;
}

void validateReferences(Diagnostics &diagnostics,
                        const std::filesystem::path &path,
                        const std::string &text) {
  const std::optional<std::filesystem::path> projectDirectory =
      findProjectDirectory(path);
  if (!projectDirectory.has_value()) {
    return;
  }

  const AssetRegistry registry = loadAssetRegistry(*projectDirectory);
  diagnostics.insert(diagnostics.end(), registry.diagnostics.begin(),
                     registry.diagnostics.end());

  for (const std::string &reference : extractAssetReferences(text)) {
    const AssetManifest *asset = findAsset(registry, reference);
    if (asset == nullptr) {
      diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "ASSET_REFERENCE_NOT_FOUND",
          .message =
              "Referenced asset was not found in project asset manifests: " +
              reference,
          .path = path.string(),
          .suggestion = "Add an .asset.json manifest under the project assets "
                        "directory with this id.",
      });
      continue;
    }

    for (const std::filesystem::path &sourcePath : asset->sourcePaths) {
      if (!std::filesystem::exists(sourcePath)) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "ASSET_SOURCE_NOT_FOUND",
            .message = "Asset source file does not exist for " + reference,
            .path = asset->manifestPath.string(),
            .suggestion = "Create the source file or update the asset manifest "
                          "source path.",
        });
      }
    }
  }

  for (const std::string &reference : extractScriptReferences(text)) {
    const std::filesystem::path scriptPath =
        resolveScriptReference(*projectDirectory, reference);
    if (!std::filesystem::exists(scriptPath)) {
      diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "SCRIPT_REFERENCE_NOT_FOUND",
          .message = "Referenced Lua script does not exist: " + reference,
          .path = path.string(),
          .suggestion = "Create the Lua script under the project directory or "
                        "update the script:// reference.",
      });
    }
  }
}

void validateDuplicateEntityIds(Diagnostics &diagnostics,
                                const std::filesystem::path &path,
                                const std::string &text) {
  std::set<std::string> ids;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t key = text.find("\"id\"", cursor);
    if (key == std::string::npos) {
      break;
    }
    const std::size_t colon = text.find(':', key + 4);
    const std::size_t quote = colon == std::string::npos
                                  ? std::string::npos
                                  : text.find('"', colon + 1);
    const std::size_t end = quote == std::string::npos
                                ? std::string::npos
                                : text.find('"', quote + 1);
    if (quote == std::string::npos || end == std::string::npos) {
      break;
    }
    const std::string id = text.substr(quote + 1, end - quote - 1);
    if (!id.starts_with("scene://") && !ids.insert(id).second) {
      diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "SCENE_DUPLICATE_ENTITY_ID",
          .message = "Scene contains duplicate entity id: " + id,
          .path = path.string(),
          .suggestion = "Use stable unique ids for every entity.",
      });
    }
    cursor = end + 1;
  }
}

void validateSceneComponents(Diagnostics &diagnostics,
                             const std::filesystem::path &path,
                             const std::string &text) {
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error &error) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error,
                                     .code = "INVALID_JSON",
                                     .message = error.what(),
                                     .path = path.string(),
                                     .suggestion = "Fix the JSON syntax."});
    return;
  }
  if (!document.contains("entities") || !document["entities"].is_array()) {
    return;
  }
  for (const auto &entity : document["entities"]) {
    if (!entity.is_object() || !entity.contains("components") ||
        !entity["components"].is_object()) {
      continue;
    }
    const std::string entityId = entity.value("id", "ent_unknown");
    for (auto component = entity["components"].begin();
         component != entity["components"].end(); ++component) {
      const auto *descriptor =
          runtime::scene_loading::findComponentDescriptor(component.key());
      if (descriptor == nullptr) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "SCENE_UNKNOWN_COMPONENT",
            .message = "Entity " + entityId +
                       " uses unknown component: " + component.key(),
            .path = path.string(),
            .suggestion = "Register an engine/extension component or move "
                          "gameplay data into LuaScript.properties."});
        continue;
      }
      for (const auto &error : runtime::scene_loading::validateComponent(
               *descriptor, component.value())) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "SCENE_INVALID_COMPONENT_FIELD",
            .message =
                "Entity " + entityId + ", component " + component.key() +
                (error.field.empty() ? std::string{} : "." + error.field) +
                " " + error.message,
            .path = path.string(),
            .suggestion = "Use the component registry schema for valid fields "
                          "and types."});
      }
      if (component.key() == "AnimationStateMachine" &&
          component.value().is_object()) {
        const auto &machine = component.value();
        std::set<std::string> states;
        if (machine.contains("states") && machine["states"].is_object())
          for (const auto &[name, unused] : machine["states"].items()) {
            (void)unused;
            states.insert(name);
          }
        const auto reportMissing = [&](const std::string &kind,
                                       const std::string &name) {
          if (!name.empty() && name != "*" && !states.contains(name))
            diagnostics.push_back(
                {.severity = Severity::Error,
                 .code = "ANIMATION_STATE_REFERENCE_NOT_FOUND",
                 .message = "Entity " + entityId + " " + kind +
                            " references missing animation state: " + name,
                 .path = path.string(),
                 .suggestion =
                     "Add the state or correct the stable state name."});
        };
        reportMissing("initial_state", machine.value("initial_state", ""));
        if (machine.contains("transitions") &&
            machine["transitions"].is_object())
          for (const auto &[unused, transition] :
               machine["transitions"].items()) {
            (void)unused;
            if (!transition.is_object())
              continue;
            reportMissing("transition.from", transition.value("from", ""));
            reportMissing("transition.to", transition.value("to", ""));
          }
        if (machine.contains("blend_spaces") &&
            machine["blend_spaces"].is_object())
          for (const auto &[spaceName, space] :
               machine["blend_spaces"].items()) {
            if (!space.is_object() || !space.contains("points") ||
                !space["points"].is_array())
              continue;
            for (const auto &point : space["points"])
              if (point.is_object())
                reportMissing("blend space " + spaceName,
                              point.value("state", ""));
          }
        if (machine.contains("layers") && machine["layers"].is_object())
          for (const auto &[layerName, layer] : machine["layers"].items())
            if (layer.is_object())
              reportMissing("layer " + layerName, layer.value("state", ""));
      }
    }
  }
}

void validateTransform3DHierarchy(Diagnostics &diagnostics,
                                  const std::filesystem::path &path,
                                  const nlohmann::json &document) {
  if (!document.contains("entities") || !document["entities"].is_array())
    return;
  std::map<std::string, std::string> parents;
  for (const auto &entity : document["entities"]) {
    if (!entity.is_object() || !entity.contains("id") ||
        !entity["id"].is_string() || !entity.contains("components") ||
        !entity["components"].is_object())
      continue;
    const auto transform = entity["components"].find("Transform3D");
    if (transform == entity["components"].end() || !transform->is_object())
      continue;
    const auto parent = transform->find("parent");
    if (parent != transform->end() && parent->is_string() &&
        !parent->get<std::string>().empty())
      parents[entity["id"].get<std::string>()] = parent->get<std::string>();
    else
      parents.try_emplace(entity["id"].get<std::string>(), "");
  }

  std::set<std::string> reportedCycles;
  for (const auto &[entityId, parentId] : parents) {
    if (!parentId.empty() && !parents.contains(parentId))
      diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "TRANSFORM3D_PARENT_NOT_FOUND",
          .message = "Transform3D parent was not found for " + entityId + ": " +
                     parentId,
          .path = path.string(),
          .suggestion = "Reference an entity with Transform3D by stable ID."});
    std::unordered_set<std::string> visiting;
    std::string current = entityId;
    while (parents.contains(current) && !parents.at(current).empty()) {
      if (!visiting.insert(current).second) {
        if (reportedCycles.insert(current).second)
          diagnostics.push_back(Diagnostic{
              .severity = Severity::Error,
              .code = "TRANSFORM3D_HIERARCHY_CYCLE",
              .message =
                  "Transform3D hierarchy cycle includes entity: " + current,
              .path = path.string(),
              .suggestion = "Remove one parent edge from the cycle."});
        break;
      }
      current = parents.at(current);
    }
  }
}

void validatePhysics3D(Diagnostics &diagnostics,
                       const std::filesystem::path &path,
                       const nlohmann::json &document) {
  if (!document.contains("entities") || !document["entities"].is_array())
    return;
  constexpr std::array colliderNames{"BoxCollider3D", "SphereCollider3D",
                                     "CapsuleCollider3D", "ConvexCollider3D",
                                     "ModelCollider3D"};
  for (const auto &entity : document["entities"]) {
    if (!entity.is_object() || !entity.contains("components") ||
        !entity["components"].is_object())
      continue;
    const std::string id = entity.value("id", "ent_unknown");
    const auto &components = entity["components"];
    const auto body = components.find("Rigidbody3D");
    const auto character = components.find("CharacterController3D");
    const std::string bodyType = body != components.end() && body->is_object()
                                     ? body->value("body_type", "static")
                                     : "static";
    const int colliderCount = static_cast<int>(
        std::ranges::count_if(colliderNames, [&](const char *name) {
          return components.contains(name);
        }));
    if (colliderCount > 1)
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_MULTIPLE_COLLIDERS",
           .message = "Entity " + id + " has multiple 3D collider components.",
           .path = path.string(),
           .suggestion = "Use one explicit collider per entity; compose a "
                         "compound from child entities."});
    if (body != components.end() && bodyType != "static" && colliderCount == 0)
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_BODY_REQUIRES_COLLIDER",
           .message = "Moving Rigidbody3D " + id + " has no collider.",
           .path = path.string(),
           .suggestion = "Add a box, sphere, capsule, or convex collider."});
    if (character != components.end() && colliderCount == 0)
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_CHARACTER_REQUIRES_COLLIDER",
           .message = "CharacterController3D " + id + " has no collider.",
           .path = path.string(),
           .suggestion = "Add a box, sphere, capsule, or convex collider to "
                         "select the character shape."});
    if (character != components.end() && body != components.end())
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_CHARACTER_CONFLICTS_WITH_BODY",
           .message =
               "CharacterController3D " + id + " also has a Rigidbody3D.",
           .path = path.string(),
           .suggestion = "Use either character-controller movement or a "
                         "rigidbody on one entity, not both."});
    if (character != components.end() && components.contains("ModelCollider3D"))
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_CHARACTER_REQUIRES_CONVEX_COLLIDER",
           .message = "CharacterController3D " + id +
                      " uses an unsupported triangle-mesh collider.",
           .path = path.string(),
           .suggestion = "Use a box, sphere, capsule, or convex collider."});
    if (character != components.end()) {
      for (const char *name : colliderNames) {
        const auto collider = components.find(name);
        if (collider != components.end() && collider->is_object() &&
            collider->value("is_trigger", false))
          diagnostics.push_back(
              {.severity = Severity::Error,
               .code = "PHYSICS3D_CHARACTER_COLLIDER_CANNOT_BE_TRIGGER",
               .message = "CharacterController3D " + id +
                          " uses a trigger as its movement shape.",
               .path = path.string(),
               .suggestion = "Set is_trigger to false; use a separate entity "
                             "for trigger volumes."});
      }
    }
    if (components.contains("ModelCollider3D") && bodyType != "static")
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_MESH_REQUIRES_STATIC_BODY",
           .message =
               "Triangle-mesh collider " + id + " must use a static body.",
           .path = path.string(),
           .suggestion = "Use ConvexCollider3D for moving bodies."});
    if (body != components.end() && bodyType != "static") {
      const auto transform = components.find("Transform3D");
      if (transform != components.end() && transform->is_object() &&
          !transform->value("parent", "").empty())
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "PHYSICS3D_MOVING_BODY_REQUIRES_ROOT_TRANSFORM",
             .message = "Moving Rigidbody3D " + id +
                        " cannot have a parent Transform3D.",
             .path = path.string(),
             .suggestion = "Keep the physics body at the scene root and "
                           "parent visual children to it."});
    }
    if (character != components.end()) {
      const auto transform = components.find("Transform3D");
      if (transform != components.end() && transform->is_object() &&
          !transform->value("parent", "").empty())
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "PHYSICS3D_CHARACTER_REQUIRES_ROOT_TRANSFORM",
             .message = "CharacterController3D " + id +
                        " cannot have a parent Transform3D.",
             .path = path.string(),
             .suggestion = "Keep the controller at the scene root and parent "
                           "visual children to it."});
    }
    const auto capsule = components.find("CapsuleCollider3D");
    if (capsule != components.end() && capsule->is_object() &&
        capsule->value("height", 1.8F) < 2.0F * capsule->value("radius", 0.4F))
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PHYSICS3D_CAPSULE_HEIGHT_TOO_SMALL",
           .message = "Capsule collider " + id +
                      " height must be at least twice its radius.",
           .path = path.string(),
           .suggestion = "Increase height or reduce radius."});
    const auto convex = components.find("ConvexCollider3D");
    if (convex != components.end() && convex->is_object()) {
      const auto points = convex->find("points");
      if (points == convex->end() || !points->is_array() || points->size() < 4)
        diagnostics.push_back(
            {.severity = Severity::Error,
             .code = "PHYSICS3D_CONVEX_REQUIRES_FOUR_POINTS",
             .message =
                 "Convex collider " + id + " needs at least four points.",
             .path = path.string(),
             .suggestion = "Provide a non-coplanar convex point set."});
    }
  }
}

} // namespace

SourceFileKind classifySourceFile(const std::filesystem::path &path) {
  if (isAssetGroupFile(path)) {
    return SourceFileKind::AssetGroup;
  }
  if (isPackageManifestFile(path)) {
    return SourceFileKind::Package;
  }
  if (isInputReplayFile(path)) {
    return SourceFileKind::InputReplay;
  }
  if (isUiPrefabFile(path)) {
    return SourceFileKind::UiPrefab;
  }
  if (isPrefabFile(path)) {
    return SourceFileKind::Prefab;
  }
  if (isProjectFile(path)) {
    return SourceFileKind::Project;
  }
  if (isSceneFile(path)) {
    return SourceFileKind::Scene;
  }
  if (isHudFile(path)) {
    return SourceFileKind::Hud;
  }
  if (isSaveFile(path)) {
    return SourceFileKind::Save;
  }
  if (isAssetFile(path)) {
    return SourceFileKind::Asset;
  }
  return SourceFileKind::Unknown;
}

ValidationSummary validatePath(const std::filesystem::path &path) {
  ValidationSummary summary;

  if (!std::filesystem::exists(path)) {
    summary.diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = "PATH_NOT_FOUND",
        .message = "Path does not exist.",
        .path = path.string(),
        .suggestion =
            "Pass a project, scene, prefab, HUD, asset, save, or directory "
            "path that exists.",
    });
    return summary;
  }

  const std::vector<std::filesystem::path> files =
      collectKnownSourceFiles(path);
  if (files.empty()) {
    summary.diagnostics.push_back(Diagnostic{
        .severity = Severity::Warning,
        .code = "NO_SOURCE_FILES",
        .message = "No recognized DemiEngine source files were found to "
                   "validate.",
        .path = path.string(),
        .suggestion = "Add a recognized DemiEngine source file such as "
                      ".project.json, .scene.json, .prefab.json, "
                      ".ui.prefab.json, .hud.json, .asset.json, or "
                      ".save.json.",
    });
    return summary;
  }

  for (const std::filesystem::path &file : files) {
    ++summary.checkedFiles;
    const SourceFileKind kind = classifySourceFile(file);
    Diagnostics fileDiagnostics = validateTextFile(file, kind);
    if (kind == SourceFileKind::Project &&
        readFile(file).find("\"performance_budgets\"") != std::string::npos) {
      const auto budget = assets::inspectSceneBudget3D(file, "android");
      fileDiagnostics.insert(fileDiagnostics.end(), budget.diagnostics.begin(),
                             budget.diagnostics.end());
    }
    summary.diagnostics.insert(summary.diagnostics.end(),
                               fileDiagnostics.begin(), fileDiagnostics.end());
  }

  if (const auto projectDirectory = findProjectDirectory(path)) {
    const Diagnostics assetDiagnostics =
        validateAssetRegistry(loadAssetRegistry(*projectDirectory));
    summary.diagnostics.insert(summary.diagnostics.end(),
                               assetDiagnostics.begin(),
                               assetDiagnostics.end());
  }

  return summary;
}

Diagnostics validateTextFile(const std::filesystem::path &path,
                             const SourceFileKind kind) {
  Diagnostics diagnostics;
  if (kind == SourceFileKind::Unknown) {
    diagnostics.push_back(Diagnostic{
        .severity = Severity::Warning,
        .code = "UNKNOWN_SOURCE_KIND",
        .message = "File is not a known DemiEngine source data file.",
        .path = path.string(),
        .suggestion = "Use a recognized source suffix such as .project.json, "
                      ".scene.json, .prefab.json, .ui.prefab.json, "
                      ".hud.json, .asset.json, or .save.json.",
    });
    return diagnostics;
  }

  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = "FILE_READ_FAILED",
        .message = "Failed to open file for validation.",
        .path = path.string(),
        .suggestion = "Check file permissions and path spelling.",
    });
    return diagnostics;
  }

  const std::string text = readFile(path);
  requireToken(diagnostics, text, path, "\"format_version\"",
               "MISSING_FORMAT_VERSION",
               "Source data file is missing format_version.",
               "Add an integer format_version field at the top level.");

  switch (kind) {
  case SourceFileKind::Project:
    requireToken(diagnostics, text, path, "\"name\"", "PROJECT_MISSING_NAME",
                 "Project file is missing name.",
                 "Add a top-level name field.");
    requireToken(diagnostics, text, path, "\"scenes\"",
                 "PROJECT_MISSING_SCENES", "Project file is missing scenes.",
                 "Add a scenes array with scene:// references.");
    try {
      const auto project = nlohmann::json::parse(text);
      if (const auto declared = project.find("packages");
          declared != project.end()) {
        if (!declared->is_object()) {
          diagnostics.push_back(
              {.severity = Severity::Error,
               .code = "PROJECT_PACKAGES_INVALID",
               .message =
                   "Project packages must be a name-to-constraint object.",
               .path = path.string(),
               .suggestion =
                   "Use packages: {\"demi.gameplay.health\": \"^1.0.0\"}."});
        } else {
          for (const auto &[name, constraint] : declared->items())
            if (!packages::validPackageName(name) || !constraint.is_string() ||
                !packages::VersionConstraint::parse(
                    constraint.is_string() ? constraint.get<std::string>()
                                           : ""))
              diagnostics.push_back(
                  {.severity = Severity::Error,
                   .code = "PROJECT_PACKAGE_REQUIREMENT_INVALID",
                   .message = "Invalid package requirement: " + name,
                   .path = path.string(),
                   .suggestion = "Use a lowercase package name and "
                                 "semantic-version constraint."});
        }
      }
    } catch (const nlohmann::json::parse_error &) {
      // Other project validation reports malformed JSON.
    }
    break;
  case SourceFileKind::Package: {
    const auto loaded = packages::loadPackageManifest(path);
    diagnostics.insert(diagnostics.end(), loaded.diagnostics.begin(),
                       loaded.diagnostics.end());
    break;
  }
  case SourceFileKind::AssetGroup: {
    const auto group = assets::loadAssetGroup(path, &diagnostics);
    if (group) {
      const auto projectDirectory = findProjectDirectory(path);
      if (projectDirectory) {
        std::vector<std::string> declaredScenes;
        for (const auto &source : collectKnownSourceFiles(*projectDirectory))
          if (isProjectFile(source)) {
            declaredScenes = extractSceneReferences(source);
            break;
          }
        (void)assets::resolveAssetGroup(
            *group, loadAssetRegistry(*projectDirectory),
            [&](const std::string_view root, Diagnostics *issues) {
              if (root.starts_with("scene://") &&
                  std::ranges::find(declaredScenes, root) !=
                      declaredScenes.end())
                return std::vector<std::string>{};
              if (issues != nullptr)
                issues->push_back(
                    {.severity = Severity::Error,
                     .code = root.starts_with("scene://")
                                 ? "ASSET_GROUP_SCENE_ROOT_NOT_FOUND"
                                 : "ASSET_GROUP_ROOT_UNSUPPORTED",
                     .message = root.starts_with("scene://")
                                    ? "Asset-group scene root is not declared "
                                      "by the project: " +
                                          std::string(root)
                                    : "Unsupported asset-group root: " +
                                          std::string(root),
                     .path = path.string(),
                     .suggestion = {}});
              return std::vector<std::string>{};
            },
            &diagnostics);
      }
    }
    break;
  }
  case SourceFileKind::Scene:
    requireToken(diagnostics, text, path, "\"id\"", "SCENE_MISSING_ID",
                 "Scene file is missing id.",
                 "Add a stable scene id such as scene://main.");
    requireToken(diagnostics, text, path, "\"entities\"",
                 "SCENE_MISSING_ENTITIES", "Scene file is missing entities.",
                 "Add an entities array, even if it is empty.");
    validateDuplicateEntityIds(diagnostics, path, text);
    validateSceneComponents(diagnostics, path, text);
    try {
      const auto expansion =
          runtime::composition::expandScene(path, nlohmann::json::parse(text));
      diagnostics.insert(diagnostics.end(), expansion.diagnostics.begin(),
                         expansion.diagnostics.end());
      if (expansion.document)
        validateTransform3DHierarchy(diagnostics, path, *expansion.document);
      if (expansion.document)
        validatePhysics3D(diagnostics, path, *expansion.document);
    } catch (const nlohmann::json::parse_error &) {
      // validateSceneComponents already reports malformed JSON.
    }
    validateReferences(diagnostics, path, text);
    break;
  case SourceFileKind::Hud:
    if (text.find("\"root\"") == std::string::npos) {
      diagnostics.push_back(
          Diagnostic{.severity = Severity::Error,
                     .code = "HUD_MISSING_CONTENT",
                     .message = "HUD file is missing a root UI node.",
                     .path = path.string(),
                     .suggestion = "Add a root object for tree UI."});
    }
    try {
      const auto expansion =
          runtime::ui::expandUiDocument(path, nlohmann::json::parse(text));
      diagnostics.insert(diagnostics.end(), expansion.diagnostics.begin(),
                         expansion.diagnostics.end());
    } catch (const nlohmann::json::parse_error &) {
      // The common JSON diagnostics already report malformed documents.
    }
    validateReferences(diagnostics, path, text);
    break;
  case SourceFileKind::Save:
    requireToken(diagnostics, text, path, "\"slot\"", "SAVE_MISSING_SLOT",
                 "Save file is missing slot.", "Add a stable save slot name.");
    requireToken(diagnostics, text, path, "\"state\"", "SAVE_MISSING_STATE",
                 "Save file is missing state.",
                 "Add a state object for game-specific data.");
    try {
      const nlohmann::json document = nlohmann::json::parse(text);
      if (document.value("state_model", std::string{}) == "game_state_v1") {
        const int version = document.value("format_version", 0);
        if (version < 1 || version > 2) {
          diagnostics.push_back(Diagnostic{
              .severity = Severity::Error,
              .code = "SAVE_INCOMPATIBLE_FORMAT",
              .message = "Structured save format_version " +
                         std::to_string(version) +
                         " is incompatible with this runtime (supported: 1-2).",
              .path = path.string(),
              .suggestion =
                  "Register and run a save migration before loading it."});
        }
        if (!document.contains("metadata") ||
            !document["metadata"].is_object()) {
          diagnostics.push_back(Diagnostic{
              .severity = Severity::Error,
              .code = "SAVE_MISSING_METADATA",
              .message = "Structured save is missing autosave metadata.",
              .path = path.string(),
              .suggestion =
                  "Add metadata with autosave, sequence, and reason."});
        }
        if (document.contains("state") && document["state"].is_object()) {
          for (const char *section :
               {"game", "selected_entities", "prefab_instances", "lua"}) {
            if (!document["state"].contains(section) ||
                !document["state"][section].is_object()) {
              diagnostics.push_back(Diagnostic{
                  .severity = Severity::Error,
                  .code = "SAVE_MISSING_STATE_SECTION",
                  .message = std::string(
                                 "Structured save is missing state section: ") +
                             section,
                  .path = path.string(),
                  .suggestion = "Write saves through Save.write_state."});
            }
          }
        }
      }
    } catch (const nlohmann::json::parse_error &) {
      // The common JSON diagnostics already report malformed documents.
    }
    break;
  case SourceFileKind::Asset:
    requireToken(diagnostics, text, path, "\"id\"", "ASSET_MISSING_ID",
                 "Asset manifest is missing id.",
                 "Add an id such as asset://sprites/player.");
    requireToken(diagnostics, text, path, "\"type\"", "ASSET_MISSING_TYPE",
                 "Asset manifest is missing type.",
                 "Add a type such as Texture2D.");
    if (text.find("\"type\": \"ImageAnimation2D\"") != std::string::npos) {
      requireToken(diagnostics, text, path, "\"sources\"",
                   "ASSET_MISSING_SOURCES",
                   "ImageAnimation2D asset is missing sources.",
                   "Add a sources array in playback order.");
    } else {
      requireToken(diagnostics, text, path, "\"source\"",
                   "ASSET_MISSING_SOURCE", "Asset manifest is missing source.",
                   "Add a source file path relative to the manifest.");
    }
    {
      Diagnostic diagnostic;
      const std::optional<AssetManifest> asset =
          loadAssetManifest(path, &diagnostic);
      if (!asset.has_value()) {
        diagnostics.push_back(diagnostic);
      } else if (std::ranges::any_of(
                     asset->sourcePaths,
                     [](const std::filesystem::path &sourcePath) {
                       return !std::filesystem::exists(sourcePath);
                     })) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "ASSET_SOURCE_NOT_FOUND",
            .message = "Asset source file does not exist.",
            .path = path.string(),
            .suggestion = "Create the source file or update the asset manifest "
                          "source path.",
        });
      } else if (asset->texturePath.has_value() &&
                 !std::filesystem::exists(*asset->texturePath)) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "ASSET_TEXTURE_NOT_FOUND",
            .message = "Asset texture file does not exist.",
            .path = path.string(),
            .suggestion = "Create the texture file or update the asset "
                          "manifest texture path.",
        });
      } else if (asset->atlasPath.has_value() &&
                 !std::filesystem::exists(*asset->atlasPath)) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "ASSET_ATLAS_NOT_FOUND",
            .message = "Asset atlas file does not exist.",
            .path = path.string(),
            .suggestion = "Create the atlas file or update the asset manifest "
                          "atlas path.",
        });
      }
    }
    break;
  case SourceFileKind::Prefab: {
    requireToken(diagnostics, text, path, "\"id\"", "PREFAB_MISSING_ID",
                 "Prefab is missing id.", "Add a prefab:// id.");
    const auto expansion = runtime::composition::inspectPrefab(path);
    diagnostics.insert(diagnostics.end(), expansion.diagnostics.begin(),
                       expansion.diagnostics.end());
    validateSceneComponents(diagnostics, path, text);
    break;
  }
  case SourceFileKind::UiPrefab: {
    requireToken(diagnostics, text, path, "\"id\"", "UI_PREFAB_MISSING_ID",
                 "UI prefab is missing id.", "Add a ui-prefab:// id.");
    const auto expansion = runtime::ui::inspectUiPrefab(path);
    diagnostics.insert(diagnostics.end(), expansion.diagnostics.begin(),
                       expansion.diagnostics.end());
    validateReferences(diagnostics, path, text);
    break;
  }
  case SourceFileKind::InputReplay:
    requireToken(diagnostics, text, path, "\"fixed_timestep\"",
                 "REPLAY_MISSING_FIXED_TIMESTEP",
                 "Input replay is missing fixed_timestep.",
                 "Add the project simulation fixed timestep.");
    requireToken(diagnostics, text, path, "\"frames\"", "REPLAY_MISSING_FRAMES",
                 "Input replay is missing frames.",
                 "Add a deterministic frames array.");
    break;
  case SourceFileKind::Unknown:
    break;
  }

  return diagnostics;
}

std::vector<std::string>
extractSceneReferences(const std::filesystem::path &projectPath) {
  std::vector<std::string> scenes;
  const std::string text = readFile(projectPath);

  std::size_t cursor = 0;
  while (true) {
    const std::size_t found = text.find("scene://", cursor);
    if (found == std::string::npos) {
      break;
    }

    std::size_t end = found;
    while (end < text.size()) {
      const char c = text[end];
      if (c == '"' || c == '\'' || c == ',' || c == ']' || c == '}' ||
          c == '\n' || c == '\r' || c == ' ' || c == '\t') {
        break;
      }
      ++end;
    }

    const std::string reference = text.substr(found, end - found);
    if (std::ranges::find(scenes, reference) == scenes.end()) {
      scenes.push_back(reference);
    }
    cursor = end;
  }

  return scenes;
}

} // namespace demi
