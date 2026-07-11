#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void MeshRendererComponent::parse(const nlohmann::json &json, Entity &entity) {
  MeshRendererComponent component;
  component.model = scene_loading::stringOr(json, "model");
  component.shape = scene_loading::stringOr(json, "shape", "cube");
  if (auto value = scene_loading::vec3Field(json, "size"))
    component.size = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  component.texture = scene_loading::stringOr(json, "texture");
  component.wireframe =
      scene_loading::boolField(json, "wireframe").value_or(false);
  if (const auto *values = scene_loading::arrayField(json, "vertices")) {
    component.vertices.reserve(values->size());
    for (const auto &value : *values)
      if (value.is_array() && value.size() >= 3)
        component.vertices.push_back({value[0].get<float>(),
                                      value[1].get<float>(),
                                      value[2].get<float>()});
  }
  if (const auto *values = scene_loading::arrayField(json, "normals")) {
    component.normals.reserve(values->size());
    for (const auto &value : *values)
      if (value.is_array() && value.size() >= 3)
        component.normals.push_back({value[0].get<float>(),
                                     value[1].get<float>(),
                                     value[2].get<float>()});
  }
  if (const auto *values = scene_loading::arrayField(json, "uvs")) {
    component.uvs.reserve(values->size());
    for (const auto &value : *values)
      if (value.is_array() && value.size() >= 2)
        component.uvs.push_back({value[0].get<float>(), value[1].get<float>()});
  }
  entity.meshRenderer = component;
}
} // namespace demi::runtime
