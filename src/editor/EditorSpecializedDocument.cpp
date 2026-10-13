#include "editor/EditorSpecializedDocument.h"
#include "demi/assets/DataAssetContent.h"

#include "editor/EditorDocumentStore.h"

#include "demi/assets/DataAsset.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <algorithm>

namespace demi::editor {

bool EditorSpecializedDocument::open(
    const std::filesystem::path &selectedSource,
    const EditorAssetIndex &assetIndex, std::string &error) {
  std::filesystem::path path = selectedSource;
  associatedManifest_.reset();
  dataContentType_.clear();
  std::optional<AssetManifest> stagedDataManifest;
  if (isPrefabFile(path)) {
    kind_ = EditorSpecializedKind::Prefab;
  } else if (isHudFile(path)) {
    error = "HUD documents open in the integrated HUD viewport.";
    return false;
  } else {
    const EditorAssetRecord *record = assetIndex.findByManifest(path);
    if (record == nullptr)
      record = assetIndex.findBySource(path);
    if (record == nullptr) {
      error = "This source has no specialized editor.";
      return false;
    }
    if (record->manifest.type == "Material") {
      kind_ = EditorSpecializedKind::Material;
      path = record->manifest.sourcePath;
      associatedManifest_ = record->manifest.manifestPath;
    } else if (record->manifest.type == "Model3D") {
      kind_ = EditorSpecializedKind::Animation;
      path = record->manifest.manifestPath;
    } else if (record->manifest.type == "DataAsset" ||
               record->manifest.type == "DataSchema") {
      kind_ = EditorSpecializedKind::Data;
      path = record->manifest.sourcePath;
      associatedManifest_ = record->manifest.manifestPath;
      stagedDataManifest = record->manifest;
      if (const auto metadata = assets::dataAssetMetadata(record->manifest))
        dataContentType_ = metadata->contentType;
    } else if (record->manifest.type == "AudioClip") {
      kind_ = EditorSpecializedKind::Audio;
      path = record->manifest.manifestPath;
    } else {
      error = "This asset type has no specialized editor yet.";
      return false;
    }
  }
  const EditorSpecializedKind kind = kind_;
  EditorJsonValidator validator = [kind](const std::filesystem::path &source,
                                         const nlohmann::json &document) {
    return validateSpecializedDocument(kind, source, document);
  };
  if (kind == EditorSpecializedKind::Data && stagedDataManifest) {
    const AssetManifest manifest = *stagedDataManifest;
    const AssetRegistry registry = assetIndex.registry();
    validator = [kind, manifest, registry, contentType = dataContentType_](
                    const std::filesystem::path &source,
                    const nlohmann::json &document) {
      Diagnostics diagnostics =
          validateSpecializedDocument(kind, source, document, contentType);
      const auto parsed = assets::parseDataDocument(document.dump(), source);
      if (parsed.document) {
        AssetManifest proposed = manifest;
        const auto metadata = assets::dataAssetMetadata(manifest);
        if (metadata) {
          const auto content = assets::inspectDataAssetContent(
              metadata->contentType, *parsed.document, registry, manifest.id);
          diagnostics.insert(diagnostics.end(), content.diagnostics.begin(),
                             content.diagnostics.end());
          // Native content references are declared by reimport on Save. Use
          // that prospective set while validating an unsaved edit; the
          // manifest on disk is never modified by field controls.
          proposed.dependencies.insert(proposed.dependencies.end(),
                                       content.dependencies.begin(),
                                       content.dependencies.end());
          std::ranges::sort(proposed.dependencies);
          proposed.dependencies.erase(std::unique(proposed.dependencies.begin(),
                                                  proposed.dependencies.end()),
                                      proposed.dependencies.end());
        }
        Diagnostics schemaDiagnostics = assets::validateDataAssetDocument(
            proposed, *parsed.document, registry);
        diagnostics.insert(diagnostics.end(), schemaDiagnostics.begin(),
                           schemaDiagnostics.end());
      }
      return diagnostics;
    };
  }
  if (!document_.open(path, std::move(validator), error))
    return false;
  rebuildPreview();
  return true;
}

std::string_view EditorSpecializedDocument::title() const {
  switch (kind_) {
  case EditorSpecializedKind::Prefab:
    return "Prefab Editor";
  case EditorSpecializedKind::Hud:
    return "HUD Editor";
  case EditorSpecializedKind::Material:
    return "Material Editor";
  case EditorSpecializedKind::Animation:
    return "Animation Editor";
  case EditorSpecializedKind::Data:
    if (dataContentType_ == "terrain_material")
      return "Terrain Surface Material";
    if (dataContentType_ == "terrain_material_set")
      return "Terrain Material Set";
    if (dataContentType_ == "terrain_palette")
      return "Terrain Asset Palette";
    return "Data Asset Editor";
  case EditorSpecializedKind::Audio:
    return "Audio Editor";
  }
  return "Document Editor";
}

void EditorSpecializedDocument::rebuildPreview() {
  expandedPrefab_ = nlohmann::json::object();
  prefabDiff_ = nlohmann::json::array();
  if (kind_ == EditorSpecializedKind::Prefab) {
    nlohmann::json scene{
        {"format_version", 1},
        {"id", "scene://prefab-editor"},
        {"entities",
         document_.json().value("entities", nlohmann::json::array())}};
    if (document_.json().contains("instances"))
      scene["instances"] = document_.json()["instances"];
    const auto expanded =
        runtime::composition::expandScene(document_.path(), scene);
    if (expanded.document) {
      expandedPrefab_ = *expanded.document;
      prefabDiff_ = nlohmann::json::diff(scene, expandedPrefab_);
    }
  }
}

bool EditorSpecializedDocument::revertPrefabOverrides(
    const std::size_t instanceIndex, std::string &error) {
  if (kind_ != EditorSpecializedKind::Prefab ||
      !document_.json().contains("instances") ||
      !document_.json()["instances"].is_array() ||
      instanceIndex >= document_.json()["instances"].size()) {
    error = "The prefab instance no longer exists.";
    return false;
  }
  const std::string pointer =
      "/instances/" + std::to_string(instanceIndex) + "/overrides";
  if (!document_.json()["instances"][instanceIndex].contains("overrides")) {
    error = "The prefab instance has no overrides to revert.";
    return false;
  }
  if (!document_.erase(pointer, error))
    return false;
  rebuildPreview();
  return true;
}

bool EditorSpecializedDocument::applyPrefabOverrides(
    const std::size_t instanceIndex, std::string &error) {
  if (kind_ != EditorSpecializedKind::Prefab || document_.isDirty()) {
    error = document_.isDirty()
                ? "Save or undo other prefab edits before applying overrides."
                : "Only prefab documents can apply nested overrides.";
    return false;
  }
  const auto instances = document_.json().find("instances");
  if (instances == document_.json().end() || !instances->is_array() ||
      instanceIndex >= instances->size()) {
    error = "The prefab instance no longer exists.";
    return false;
  }
  const nlohmann::json &instance = (*instances)[instanceIndex];
  const auto overrides = instance.find("overrides");
  if (!instance.is_object() || overrides == instance.end() ||
      !overrides->is_object()) {
    error = "The prefab instance has no object overrides to apply.";
    return false;
  }
  const auto targetPath = runtime::composition::resolvePrefabReference(
      document_.path(), instance.value("prefab", ""));
  if (!targetPath) {
    error = "The nested prefab reference cannot be resolved.";
    return false;
  }

  EditorDocumentStore store;
  std::string originalText;
  FileRevision originalRevision;
  if (!store.read(*targetPath, originalText, originalRevision, error))
    return false;
  nlohmann::json target;
  try {
    target = nlohmann::json::parse(originalText);
  } catch (const nlohmann::json::exception &exception) {
    error = exception.what();
    return false;
  }
  auto entities = target.find("entities");
  if (entities == target.end() || !entities->is_array()) {
    error = "The nested prefab has no editable entities array.";
    return false;
  }
  for (const auto &[localId, overrideValue] : overrides->items()) {
    const auto entity = std::ranges::find_if(*entities, [&](const auto &item) {
      return item.is_object() && item.value("id", "") == localId;
    });
    if (entity == entities->end()) {
      error = "An override references a missing prefab entity: " + localId;
      return false;
    }
    if (overrideValue.is_null())
      entities->erase(entity);
    else if (overrideValue.is_object()) {
      *entity = runtime::composition::mergeOverride(*entity, overrideValue);
      (*entity)["id"] = localId;
    } else {
      error = "Prefab overrides must be objects or null.";
      return false;
    }
  }
  const Diagnostics targetDiagnostics = validateSpecializedDocument(
      EditorSpecializedKind::Prefab, *targetPath, target);
  if (hasErrors(targetDiagnostics)) {
    error = targetDiagnostics.front().message;
    return false;
  }

  const std::string overridePointer =
      "/instances/" + std::to_string(instanceIndex) + "/overrides";
  if (!document_.erase(overridePointer, error))
    return false;
  FileRevision targetRevision;
  if (store.writeIfUnchanged(*targetPath, target.dump(2) + '\n',
                             originalRevision, targetRevision,
                             error) != DocumentWriteStatus::Written) {
    std::string undoError;
    (void)document_.undo(undoError);
    return false;
  }
  if (!document_.save(error)) {
    FileRevision rollbackRevision;
    std::string rollbackError;
    const bool rolledBack =
        store.writeIfUnchanged(*targetPath, originalText, targetRevision,
                               rollbackRevision,
                               rollbackError) == DocumentWriteStatus::Written;
    std::string undoError;
    (void)document_.undo(undoError);
    if (!rolledBack)
      error += " Target prefab rollback also failed: " + rollbackError;
    return false;
  }
  rebuildPreview();
  return true;
}

} // namespace demi::editor
