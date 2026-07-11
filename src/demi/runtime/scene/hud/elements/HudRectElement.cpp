#include "demi/runtime/scene/hud/elements/HudRectElement.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"
namespace demi::runtime {
void HudRectElement::parse(const nlohmann::json &json, const std::string &id,
                           World &world) {
  HudRectElement element;
  element.id = id;
  element.group = scene_loading::stringOr(json, "group");
  element.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(0.0F));
  if (auto value = scene_loading::vec2Field(json, "position"))
    element.position = *value;
  if (auto value = scene_loading::vec2Field(json, "size"))
    element.size = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    element.color = *value;
  element.visible = scene_loading::boolField(json, "visible").value_or(true);
  world.hudRects.push_back(std::move(element));
}
} // namespace demi::runtime
