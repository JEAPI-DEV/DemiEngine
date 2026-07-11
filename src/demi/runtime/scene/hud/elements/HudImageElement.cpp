#include "demi/runtime/scene/hud/elements/HudImageElement.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/World.h"
namespace demi::runtime {
void HudImageElement::parse(const nlohmann::json &json, const std::string &id,
                            World &world) {
  HudImageElement element;
  element.id = id;
  element.group = scene_loading::stringOr(json, "group");
  element.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(1.0F));
  element.texture = scene_loading::stringOr(json, "texture");
  if (auto value = scene_loading::vec2Field(json, "position"))
    element.position = *value;
  if (auto value = scene_loading::vec2Field(json, "size"))
    element.size = *value;
  if (auto value = scene_loading::vec2Field(json, "source_position"))
    element.sourcePosition = *value;
  if (auto value = scene_loading::vec2Field(json, "source_size"))
    element.sourceSize = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    element.color = *value;
  element.visible = scene_loading::boolField(json, "visible").value_or(true);
  world.hudImages.push_back(std::move(element));
}
} // namespace demi::runtime
