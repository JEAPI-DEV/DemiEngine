#include "demi/runtime/scene/HudParser.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/scene/hud/HudElementRegistry.h"

namespace demi::runtime::scene_loading {
namespace {

void parseHudElement(const Json &elementJson, World &world) {
  const std::string id = stringOr(elementJson, "id");
  if (id.empty())
    return;

  const HudElementDescriptor *descriptor =
      findHudElementDescriptor(stringOr(elementJson, "type"));
  if (descriptor != nullptr)
    descriptor->parse(elementJson, id, world);
}

} // namespace

void loadHudFile(World &world, const std::filesystem::path &hudPath,
                 std::string &error) {
  const std::optional<Json> document = readJsonFile(hudPath, error);
  if (!document.has_value())
    return;

  const composition::ExpansionResult expansion =
      composition::expandScene(hudPath, *document);
  if (!expansion.document.has_value()) {
    error = expansion.diagnostics.empty()
                ? "HUD prefab expansion failed: " + hudPath.string()
                : expansion.diagnostics.front().message;
    return;
  }
  const Json &expanded = *expansion.document;

  if (const std::optional<Vec2> canvasSize = vec2Field(expanded, "canvas_size");
      canvasSize.has_value() && canvasSize->x > 0.0F && canvasSize->y > 0.0F) {
    world.hudCanvasSize = *canvasSize;
  }

  const Json *elements = arrayField(expanded, "elements");
  if (elements == nullptr)
    return;

  for (const Json &elementJson : *elements) {
    if (elementJson.is_object())
      parseHudElement(elementJson, world);
  }
}

} // namespace demi::runtime::scene_loading
