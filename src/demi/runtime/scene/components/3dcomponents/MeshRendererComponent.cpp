#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
namespace demi::runtime {
namespace {
bool normalizedChannel(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

std::optional<float> normalizedOverride(const nlohmann::json &json,
                                        const char *name) {
  const auto found = json.find(name);
  if (found == json.end())
    return std::nullopt;
  if (!found->is_number())
    throw std::invalid_argument(std::string("MeshRenderer.") + name +
                                " must be a number from 0 to 1");
  const float value = found->get<float>();
  if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
    throw std::invalid_argument(std::string("MeshRenderer.") + name +
                                " must be finite and between 0 and 1");
  return value;
}
} // namespace

bool MeshRendererComponent::validateAuthored(const nlohmann::json &json,
                                            std::string &error) {
  if (!json.is_object()) {
    error = "MeshRenderer must be an object";
    return false;
  }
  const auto colors = json.find("vertex_colors");
  if (colors == json.end())
    return true;
  if (!colors->is_array() ||
      !std::ranges::all_of(*colors, [](const auto &color) {
        return color.is_array() && color.size() == 4 &&
               std::ranges::all_of(color, [](const auto &channel) {
                 return channel.is_number() &&
                        normalizedChannel(channel.template get<double>());
               });
      })) {
    error = "MeshRenderer.vertex_colors must be an array of RGBA colors with "
            "finite channels from 0 to 1";
    return false;
  }
  const auto vertices = json.find("vertices");
  if (!colors->empty() &&
      (vertices == json.end() || !vertices->is_array() ||
       vertices->size() != colors->size())) {
    error = "MeshRenderer.vertex_colors must be empty or match the inline "
            "vertices count";
    return false;
  }
  return true;
}

nlohmann::json MeshRendererComponent::schemaConstraints() {
  return {{"if", {{"required", {"vertex_colors"}},
                  {"properties", {{"vertex_colors", {{"minItems", 1}}}}}}},
          {"then", {{"required", {"vertices"}},
                    {"properties", {{"vertices", {{"minItems", 1}}}}}}}};
}

bool MeshRendererComponent::serializeField(
    const MeshRendererComponent &component, std::string_view field,
    nlohmann::json &out) {
  if (field == "metallic" && component.metallic) {
    out = *component.metallic;
    return true;
  }
  if (field == "roughness" && component.roughness) {
    out = *component.roughness;
    return true;
  }
  if (field == "opacity" && component.opacity) {
    out = *component.opacity;
    return true;
  }
  if (field == "surface_mode" && component.surfaceMode) {
    out = *component.surfaceMode;
    return true;
  }
  if (field != "material_properties")
    return false;
  out = nlohmann::json::object();
  for (const auto &[name, value] : component.materialNumbers)
    out[name] = value;
  for (const auto &[name, value] : component.materialColors)
    out[name] = {value.r, value.g, value.b, value.a};
  return true;
}

bool MeshRendererComponent::validateVertexColors(std::string &error) const {
  if (!vertexColors.empty() && vertexColors.size() != vertices.size()) {
    error = "MeshRenderer.vertex_colors must be empty or match the inline "
            "vertices count";
    return false;
  }
  for (const Color &value : vertexColors) {
    if (!normalizedChannel(value.r) || !normalizedChannel(value.g) ||
        !normalizedChannel(value.b) || !normalizedChannel(value.a)) {
      error = "MeshRenderer.vertex_colors channels must be finite and from 0 to 1";
      return false;
    }
  }
  return true;
}

void MeshRendererComponent::copyVertices(
    MeshRendererComponent &destination, const MeshRendererComponent &source) {
  if (!destination.vertexColors.empty() &&
      destination.vertexColors.size() != source.vertices.size())
    throw std::invalid_argument(
        "MeshRenderer.vertices must match the live vertex_colors count");
  destination.vertices = source.vertices;
}

void MeshRendererComponent::copyVertexColors(
    MeshRendererComponent &destination, const MeshRendererComponent &source) {
  if (!source.vertexColors.empty() &&
      source.vertexColors.size() != destination.vertices.size())
    throw std::invalid_argument(
        "MeshRenderer.vertex_colors must match the live inline vertices count");
  destination.vertexColors = source.vertexColors;
}

void MeshRendererComponent::markGeometryChanged() {
  std::string error;
  if (!validateVertexColors(error))
    throw std::invalid_argument(error);
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
  if (field == "vertices" || field == "normals" || field == "uvs" ||
      field == "vertex_colors") {
    mesh.markGeometryChanged();
  }
}

void MeshRendererComponent::parse(const nlohmann::json &json, Entity &entity) {
  std::string error;
  if (!validateAuthored(json, error))
    throw std::invalid_argument(error);
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
  component.metallic = normalizedOverride(json, "metallic");
  component.roughness = normalizedOverride(json, "roughness");
  component.opacity = normalizedOverride(json, "opacity");
  if (const auto found = json.find("surface_mode"); found != json.end()) {
    if (!found->is_string())
      throw std::invalid_argument("MeshRenderer.surface_mode must be opaque, transparent, or additive");
    const std::string mode = found->get<std::string>();
    if (std::ranges::find(surfaceModes, mode) == surfaceModes.end())
      throw std::invalid_argument("MeshRenderer.surface_mode must be opaque, transparent, or additive");
    component.surfaceMode = mode;
  }
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
  if (const auto *values = scene_loading::arrayField(json, "vertex_colors")) {
    component.vertexColors.reserve(values->size());
    for (const auto &value : *values)
      component.vertexColors.push_back(
          {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
           value[3].get<float>()});
  }
  if (!component.vertices.empty() || !component.vertexColors.empty()) {
    component.markGeometryChanged();
  }
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
