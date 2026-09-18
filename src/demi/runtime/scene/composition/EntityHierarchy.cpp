#include "demi/runtime/scene/composition/EntityHierarchy.h"

#include <stdexcept>

namespace demi::runtime::composition {
namespace {
using Json = nlohmann::json;
constexpr unsigned MaxDepth = 128;

std::string transformName(const Json &entity) {
  const auto &components =
      entity.contains("components") ? entity.at("components") : entity;
  std::string result;
  for (const char *name : {"Transform3D", "Transform2D", "IsoTransform"}) {
    if (!components.contains(name))
      continue;
    if (!result.empty())
      throw std::runtime_error(
          "Nested entities require one spatial transform domain");
    result = name;
  }
  if (result.empty() && entity.value("preset", "") == "prop_2d")
    result = "Transform2D";
  return result;
}

void flatten(const Json &entities, Json &output, const std::string &parent,
             const std::string &parentTransform, unsigned depth) {
  if (!entities.is_array() || depth > MaxDepth)
    throw std::runtime_error(
        "Entity children must be an array with depth at most 128");
  for (const auto &source : entities) {
    if (!source.is_object() || !source.contains("id") ||
        !source["id"].is_string() || source["id"].get<std::string>().empty())
      throw std::runtime_error(
          "Every entity requires a nonempty stable string id");
    Json entity = source;
    entity.erase("children");
    std::string transform;
    if (!parent.empty() || source.contains("children"))
      transform = transformName(entity);
    if (!parent.empty()) {
      if (parentTransform.empty() ||
          (!transform.empty() && transform != parentTransform))
        throw std::runtime_error("Nested entity " +
                                 source["id"].get<std::string>() +
                                 " must share its parent's transform domain");
      if (transform.empty())
        transform = parentTransform;
      Json &components =
          entity.contains("components") ? entity["components"] : entity;
      if (!components.contains(transform))
        components[transform] = Json::object();
      auto &value = components[transform];
      if (!value.is_object())
        throw std::runtime_error("Transform must be an object");
      if (value.contains("parent") && value["parent"] != parent)
        throw std::runtime_error("Nested entity " +
                                 source["id"].get<std::string>() +
                                 " has a conflicting explicit parent");
      value["parent"] = parent;
    }
    output.push_back(std::move(entity));
    if (source.contains("children"))
      flatten(source["children"], output, source["id"].get<std::string>(),
              transform, depth + 1);
  }
}

template <class T> T *find(T &entities, std::string_view id, unsigned depth) {
  if (!entities.is_array() || depth > MaxDepth)
    return nullptr;
  for (auto &entity : entities) {
    if (!entity.is_object())
      continue;
    if (entity.value("id", "") == id)
      return &entity;
    if (entity.contains("children"))
      if (auto *result = find(entity["children"], id, depth + 1))
        return result;
  }
  return nullptr;
}
} // namespace

nlohmann::json flattenEntityHierarchy(const nlohmann::json &entities) {
  Json result = Json::array();
  flatten(entities, result, {}, {}, 0);
  return result;
}
nlohmann::json *findAuthoredEntity(nlohmann::json &entities,
                                   std::string_view id) {
  return find(entities, id, 0);
}
const nlohmann::json *findAuthoredEntity(const nlohmann::json &entities,
                                         std::string_view id) {
  return find(entities, id, 0);
}
} // namespace demi::runtime::composition
