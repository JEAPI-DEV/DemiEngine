#include "demi/runtime/scene/hud/elements/HudTextElement.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"
namespace demi::runtime {
void HudTextElement::parse(const nlohmann::json &json, const std::string &id,
                           World &world) {
  HudTextElement element;
  element.id = id;
  element.group = scene_loading::stringOr(json, "group");
  element.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(3.0F));
  element.text = scene_loading::stringOr(json, "text");
  if (auto value = scene_loading::vec2Field(json, "position"))
    element.position = *value;
  if (auto value = scene_loading::numberField(json, "scale"))
    element.scale = *value;
  if (auto value = scene_loading::numberField(json, "font_size"))
    element.fontSize = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    element.color = *value;
  element.visible = scene_loading::boolField(json, "visible").value_or(true);
  world.hudText.push_back(std::move(element));
}
} // namespace demi::runtime
