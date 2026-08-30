#include "demi/runtime/scene/HudParser.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

namespace demi::runtime::scene_loading {
namespace {

using Json = nlohmann::json;

void mergeObject(Json &target, const Json &source, const char *field) {
  if (!source.contains(field) || !source[field].is_object())
    return;
  if (!target.contains(field) || !target[field].is_object())
    target[field] = Json::object();
  for (const auto &[key, value] : source[field].items())
    if (!target[field].contains(key))
      target[field][key] = value;
}

bool mergeUiResources(Json &document, const std::filesystem::path &hudPath,
                      std::string &error) {
  if (document.contains("theme") && document["theme"].is_string()) {
    const auto themePath =
        (hudPath.parent_path() / document["theme"].get<std::string>())
            .lexically_normal();
    const std::optional<Json> theme = readJsonFile(themePath, error);
    if (!theme.has_value())
      return false;
    mergeObject(document, *theme, "styles");
    mergeObject(document, *theme, "action_map");
  }
  if (document.contains("localization_file") &&
      document["localization_file"].is_string()) {
    const auto localizationPath =
        (hudPath.parent_path() /
         document["localization_file"].get<std::string>())
            .lexically_normal();
    const std::optional<Json> localization =
        readJsonFile(localizationPath, error);
    if (!localization.has_value())
      return false;
    mergeObject(document, *localization, "localization");
  }
  return true;
}

} // namespace

std::optional<ui::UiDocument>
parseHudDocument(const std::filesystem::path &hudPath, const Json &document,
                 std::string &error) {
  const ui::UiPrefabExpansionResult expansion =
      ui::expandUiDocument(hudPath, document);
  if (!expansion.document.has_value()) {
    error = expansion.diagnostics.empty()
                ? "HUD prefab expansion failed: " + hudPath.string()
                : expansion.diagnostics.front().message;
    return std::nullopt;
  }
  Json expanded = *expansion.document;
  if (!mergeUiResources(expanded, hudPath, error))
    return std::nullopt;

  ui::UiDocument result = ui::parseUiDocument(expanded);
  ui::UiLayoutEngine{}.layout(result, result.canvasSize);
  return result;
}

void loadHudFile(World &world, const std::filesystem::path &hudPath,
                 std::string &error) {
  const std::optional<Json> document = readJsonFile(hudPath, error);
  if (!document.has_value())
    return;
  std::optional<ui::UiDocument> parsed =
      parseHudDocument(hudPath, *document, error);
  if (!parsed)
    return;

  world.ui = std::move(*parsed);
  world.hudCanvasSize = world.ui.canvasSize;
}

} // namespace demi::runtime::scene_loading
