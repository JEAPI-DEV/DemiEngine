#include "editor/EditorSpecializedDocument.h"

#include "demi/assets/DataDocument.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiPrefabResolver.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <set>

namespace demi::editor {
namespace {

void issue(Diagnostics &diagnostics, const std::filesystem::path &path,
           std::string code, std::string message) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = {}});
}

bool sourceExists(const std::filesystem::path &manifestPath,
                  const nlohmann::json &document) {
  const std::string source = document.value("source", "");
  return !source.empty() &&
         std::filesystem::is_regular_file(manifestPath.parent_path() / source);
}

Diagnostics validatePrefab(const std::filesystem::path &path,
                           const nlohmann::json &document) {
  Diagnostics diagnostics;
  if (!document.is_object() || document.value("format_version", 0) != 1 ||
      !document.value("id", "").starts_with("prefab://") ||
      !document.contains("entities") || !document["entities"].is_array()) {
    issue(diagnostics, path, "PREFAB_INVALID_DOCUMENT",
          "Prefab requires format_version 1, a prefab:// ID, and entities.");
    return diagnostics;
  }
  nlohmann::json scene{{"format_version", 1},
                       {"id", "scene://prefab-editor"},
                       {"entities", document["entities"]}};
  if (document.contains("instances"))
    scene["instances"] = document["instances"];
  Diagnostics sceneDiagnostics = validateSceneDocument(path, scene);
  diagnostics.insert(diagnostics.end(), sceneDiagnostics.begin(),
                     sceneDiagnostics.end());
  return diagnostics;
}

Diagnostics validateHud(const std::filesystem::path &path,
                        const nlohmann::json &document) {
  Diagnostics diagnostics;
  if (!document.is_object() || document.value("format_version", 0) != 1 ||
      !document.contains("root") || !document["root"].is_object()) {
    issue(diagnostics, path, "HUD_INVALID_DOCUMENT",
          "HUD requires format_version 1 and a root node.");
    return diagnostics;
  }
  const runtime::ui::UiPrefabExpansionResult expansion =
      runtime::ui::expandUiDocument(path, document);
  diagnostics.insert(diagnostics.end(), expansion.diagnostics.begin(),
                     expansion.diagnostics.end());
  if (!expansion.document)
    return diagnostics;
  runtime::ui::UiDocument parsed =
      runtime::ui::parseUiDocument(*expansion.document);
  std::set<std::string> ids;
  for (const runtime::ui::UiNode &node : parsed.nodes)
    if (!ids.insert(node.id).second)
      issue(diagnostics, path, "HUD_DUPLICATE_NODE_ID",
            "HUD node IDs must be unique: " + node.id);
  if (parsed.nodes.empty())
    issue(diagnostics, path, "HUD_EMPTY_TREE",
          "HUD root must produce at least one UI node.");
  runtime::ui::UiLayoutEngine{}.layout(parsed, parsed.canvasSize);
  return diagnostics;
}

Diagnostics validateMaterial(const std::filesystem::path &path,
                             const nlohmann::json &document) {
  Diagnostics diagnostics;
  (void)assets::parseMaterialAsset(document, path, &diagnostics);
  return diagnostics;
}

Diagnostics validateAnimation(const std::filesystem::path &path,
                              const nlohmann::json &document) {
  Diagnostics diagnostics;
  if (document.value("format_version", 0) != 1 ||
      document.value("type", "") != "Model3D" ||
      !sourceExists(path, document)) {
    issue(diagnostics, path, "ANIMATION_MANIFEST_INVALID",
          "Animation editor requires a valid Model3D asset manifest.");
    return diagnostics;
  }
  const auto settings = document.value("settings", nlohmann::json::object());
  Diagnostics settingsDiagnostics =
      validateModelAnimationSettings(settings, path);
  diagnostics.insert(diagnostics.end(), settingsDiagnostics.begin(),
                     settingsDiagnostics.end());
  return diagnostics;
}

Diagnostics validateAudio(const std::filesystem::path &path,
                          const nlohmann::json &document) {
  Diagnostics diagnostics;
  if (document.value("format_version", 0) != 1 ||
      document.value("type", "") != "AudioClip" ||
      !sourceExists(path, document))
    issue(diagnostics, path, "AUDIO_MANIFEST_INVALID",
          "Audio editor requires a valid AudioClip asset manifest.");
  const auto settings = document.value("settings", nlohmann::json::object());
  Diagnostics settingsDiagnostics = validateAudioClipSettings(settings, path);
  diagnostics.insert(diagnostics.end(), settingsDiagnostics.begin(),
                     settingsDiagnostics.end());
  return diagnostics;
}

Diagnostics validateData(const std::filesystem::path &path,
                         const nlohmann::json &document) {
  Diagnostics diagnostics =
      assets::parseDataDocument(document.dump(), path).diagnostics;
  if (document.value("format_version", 0) != 1)
    issue(diagnostics, path, "DATA_FORMAT_VERSION_UNSUPPORTED",
          "Data documents require format_version 1.");
  return diagnostics;
}

} // namespace

Diagnostics validateSpecializedDocument(const EditorSpecializedKind kind,
                                        const std::filesystem::path &path,
                                        const nlohmann::json &document) {
  switch (kind) {
  case EditorSpecializedKind::Prefab:
    return validatePrefab(path, document);
  case EditorSpecializedKind::Hud:
    return validateHud(path, document);
  case EditorSpecializedKind::Material:
    return validateMaterial(path, document);
  case EditorSpecializedKind::Animation:
    return validateAnimation(path, document);
  case EditorSpecializedKind::Data:
    return validateData(path, document);
  case EditorSpecializedKind::Audio:
    return validateAudio(path, document);
  }
  return {};
}

} // namespace demi::editor
