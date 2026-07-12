#include "demi/runtime/scene/hud/elements/HudButtonElement.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"
namespace demi::runtime {
void HudButtonElement::parse(const nlohmann::json &json, const std::string &id,
                             World &world) {
  HudButtonElement element;
  element.id = id;
  element.group = scene_loading::stringOr(json, "group");
  element.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(2.0F));
  element.label = scene_loading::stringOr(json, "label", id);
  if (auto value = scene_loading::vec2Field(json, "position"))
    element.position = *value;
  if (auto value = scene_loading::vec2Field(json, "size"))
    element.size = *value;
  if (auto value = scene_loading::numberField(json, "scale"))
    element.scale = *value;
  if (auto value = scene_loading::numberField(json, "font_size"))
    element.fontSize = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    element.color = *value;
  if (auto value = scene_loading::colorField(json, "hover_color"))
    element.hoverColor = *value;
  if (auto value = scene_loading::colorField(json, "text_color"))
    element.textColor = *value;
  element.script = scene_loading::stringOr(json, "script");
  element.action = scene_loading::stringOr(json, "action");
  element.visible = scene_loading::boolField(json, "visible").value_or(true);
  world.hudButtons.push_back(std::move(element));
}
} // namespace demi::runtime
