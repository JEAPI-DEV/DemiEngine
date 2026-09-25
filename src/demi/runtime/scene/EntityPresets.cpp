#include "demi/runtime/scene/EntityPresets.h"
#include <nlohmann/json.hpp>

namespace demi::runtime::scene_loading {
using Json = nlohmann::json;

// P5: named entity presets expand to component blocks before parsing.
// Data-only: each preset maps to fixed component JSON with size/trigger/layer
// forwarded from the entity-level shorthand fields.
Json expandEntityPreset(const Json &entityJson) {
  if (!entityJson.is_object() || !entityJson.contains("preset") ||
      !entityJson["preset"].is_string())
    return entityJson;
  const std::string preset = entityJson["preset"].get<std::string>();
  Json expanded = entityJson;
  expanded.erase("preset");
  if (!expanded.contains("components") || !expanded["components"].is_object())
    expanded["components"] = Json::object();
  Json &components = expanded["components"];

  const auto vec3 = [&](const char *key) -> Json {
    const auto it = entityJson.find(key);
    return (it != entityJson.end() && it->is_array()) ? *it : Json{};
  };
  const Json size = vec3("size");
  const bool hasSize = !size.is_null();
  const std::string layer = entityJson.value("layer", "");
  auto ensure = [&](const char *name) -> Json & {
    if (!components.contains(name) || !components[name].is_object())
      components[name] = Json::object();
    return components[name];
  };
  if (preset == "static_box_3d") {
    Json &body = ensure("Rigidbody3D");
    if (!body.contains("body_type"))
      body["body_type"] = "static";
    if (!body.contains("use_gravity"))
      body["use_gravity"] = false;
    if (!body.contains("gravity_scale"))
      body["gravity_scale"] = 0;
    Json &box = ensure("BoxCollider3D");
    if (hasSize && !box.contains("size"))
      box["size"] = size;
    if (!layer.empty() && !box.contains("layer"))
      box["layer"] = layer;
  } else if (preset == "trigger_sphere_3d") {
    Json &sphere = ensure("SphereCollider3D");
    if (!sphere.contains("is_trigger"))
      sphere["is_trigger"] = true;
    if (!layer.empty() && !sphere.contains("layer"))
      sphere["layer"] = layer;
    if (entityJson.contains("radius") && !sphere.contains("radius"))
      sphere["radius"] = entityJson["radius"];
    Json &body = ensure("Rigidbody3D");
    if (!body.contains("body_type"))
      body["body_type"] = "static";
    if (!body.contains("use_gravity"))
      body["use_gravity"] = false;
  } else if (preset == "prop_2d") {
    Json &sprite = ensure("Sprite");
    if (entityJson.contains("texture") && !sprite.contains("texture"))
      sprite["texture"] = entityJson["texture"];
    if (!components.contains("Transform2D"))
      components["Transform2D"] = Json::object();
  } else if (preset == "character_3d") {
    Json &body = ensure("Rigidbody3D");
    if (!body.contains("body_type"))
      body["body_type"] = "dynamic";
    if (!components.contains("CharacterController3D"))
      components["CharacterController3D"] = Json::object();
    if (!components.contains("CapsuleCollider3D"))
      components["CapsuleCollider3D"] = Json::object();
  }
  // Unknown preset names pass through untouched; validation reports them.
  return expanded;
}

} // namespace demi::runtime::scene_loading
