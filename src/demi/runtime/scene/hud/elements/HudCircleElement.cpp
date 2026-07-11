#include "demi/runtime/scene/hud/elements/HudCircleElement.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"
namespace demi::runtime {
void HudCircleElement::parse(const nlohmann::json &json, const std::string &id,
                             World &world) {
  HudCircleElement element;
  element.id = id;
  element.group = scene_loading::stringOr(json, "group");
  element.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(2.0F));
  if (auto value = scene_loading::vec2Field(json, "center"))
    element.center = *value;
  if (auto value = scene_loading::numberField(json, "radius"))
    element.radius = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    element.color = *value;
  element.visible = scene_loading::boolField(json, "visible").value_or(true);
  world.hudCircles.push_back(std::move(element));
}
} // namespace demi::runtime
