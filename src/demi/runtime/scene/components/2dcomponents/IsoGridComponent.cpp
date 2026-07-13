#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void IsoGridComponent::parse(const nlohmann::json &json, Entity &entity) {
  IsoGridComponent component;
  if (auto value = scene_loading::vec2Field(json, "cell_size"))
    component.cellSize = *value;
  if (auto value = scene_loading::numberField(json, "width"))
    component.width = static_cast<int>(*value);
  if (auto value = scene_loading::numberField(json, "height"))
    component.height = static_cast<int>(*value);
  component.defaultTexture = scene_loading::stringOr(json, "default_texture");
  if (json.contains("cell_textures") && json["cell_textures"].is_object()) {
    for (const auto &[cell, texture] : json["cell_textures"].items()) {
      if (texture.is_string())
        component.cellTextures[cell] = texture.get<std::string>();
    }
  }
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
