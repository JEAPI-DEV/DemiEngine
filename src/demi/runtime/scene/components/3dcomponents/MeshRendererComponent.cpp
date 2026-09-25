#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
#include <atomic>
namespace demi::runtime {
bool MeshRendererComponent::serializeField(
    const MeshRendererComponent &component, std::string_view field,
    nlohmann::json &out) {
  if (field != "material_properties")
    return false;
  out = nlohmann::json::object();
  for (const auto &[name, value] : component.materialNumbers)
    out[name] = value;
  for (const auto &[name, value] : component.materialColors)
    out[name] = {value.r, value.g, value.b, value.a};
  return true;
}

void MeshRendererComponent::markGeometryChanged() {
  // Renderer caches are keyed by entity ID. A fresh mesh may reuse that ID,
  // so a per-component counter would repeat revisions after replacement.
  static std::atomic<std::uint64_t> nextRevision{1};
  revision = nextRevision.fetch_add(1, std::memory_order_relaxed);
  hasBounds = !vertices.empty();
  if (!hasBounds) {
    return;
  }
  boundsMin = vertices.front();
  boundsMax = vertices.front();
  for (const auto &vertex : vertices) {
    boundsMin.x = std::min(boundsMin.x, vertex.x);
    boundsMin.y = std::min(boundsMin.y, vertex.y);
    boundsMin.z = std::min(boundsMin.z, vertex.z);
    boundsMax.x = std::max(boundsMax.x, vertex.x);
    boundsMax.y = std::max(boundsMax.y, vertex.y);
    boundsMax.z = std::max(boundsMax.z, vertex.z);
  }
}

void MeshRendererComponent::afterRuntimeFieldChange(
    MeshRendererComponent &mesh, std::string_view field) {
  if (field == "vertices" || field == "normals" || field == "uvs") {
    mesh.markGeometryChanged();
  }
}

void MeshRendererComponent::parse(const nlohmann::json &json, Entity &entity) {
  MeshRendererComponent component;
  component.model = scene_loading::stringOr(json, "model");
  component.mediumLodModel = scene_loading::stringOr(json, "medium_lod_model");
  component.mediumLodDistance = std::max(
      scene_loading::numberField(json, "medium_lod_distance").value_or(0.0F),
      0.0F);
  component.lowLodModel = scene_loading::stringOr(json, "low_lod_model");
  component.lowLodDistance = std::max(
      scene_loading::numberField(json, "low_lod_distance").value_or(0.0F),
      0.0F);
  component.cullDistance = std::max(
      scene_loading::numberField(json, "cull_distance").value_or(0.0F), 0.0F);
  component.shape = scene_loading::stringOr(json, "shape", "cube");
  if (auto value = scene_loading::vec3Field(json, "size"))
    component.size = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  component.texture = scene_loading::stringOr(json, "texture");
  component.material = scene_loading::stringOr(json, "material");
  component.renderLayer = scene_loading::stringOr(json, "render_layer");
  if (const auto *properties =
          scene_loading::objectField(json, "material_properties")) {
    for (const auto &[name, value] : properties->items()) {
      if (value.is_number()) {
        component.materialNumbers.emplace(name, value.get<float>());
      } else if (value.is_array() && value.size() == 4 &&
                 std::ranges::all_of(value, [](const auto &channel) {
                   return channel.is_number();
                 })) {
        component.materialColors.emplace(
            name, Color{value[0].get<float>(), value[1].get<float>(),
                        value[2].get<float>(), value[3].get<float>()});
      }
    }
  }
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
  if (!component.vertices.empty()) {
    component.markGeometryChanged();
  }
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
