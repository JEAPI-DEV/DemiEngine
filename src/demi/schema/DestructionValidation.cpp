#include "demi/schema/DestructionValidation.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace demi {
void validateDestruction3D(Diagnostics &diagnostics,
                           const std::filesystem::path &path,
                           const nlohmann::json &document,
                           const AssetRegistry &registry) {
  if (!document.contains("entities") || !document["entities"].is_array())
    return;
  std::map<std::string, const nlohmann::json *> entities;
  bool needed = false;
  for (const auto &entity : document["entities"]) {
    if (!entity.is_object() || !entity.contains("id") ||
        !entity["id"].is_string())
      continue;
    entities.emplace(entity["id"].get<std::string>(), &entity);
    needed |= entity.contains("components") &&
              entity["components"].is_object() &&
              entity["components"].contains("Destructible3D");
  }
  if (!needed)
    return;
  const auto require = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  for (const auto &[id, entity] : entities) {
    if (!entity->contains("components") ||
        !(*entity)["components"].is_object() ||
        !(*entity)["components"].contains("Destructible3D"))
      continue;
    try {
      const auto &c = (*entity)["components"];
      require(c.contains("Transform3D") && c.contains("Rigidbody3D") &&
                  c.contains("ModelCollider3D") && !c.contains("MeshRenderer"),
              "Requires Transform3D, Rigidbody3D, ModelCollider3D and separate "
              "child visuals");
      require(!entity->value("persistent", false) &&
                  c["Transform3D"].value("parent", "").empty() &&
                  c["Rigidbody3D"].value("body_enabled", true) &&
                  !c["ModelCollider3D"].value("is_trigger", false),
              "Persistent, parented, disabled-body and trigger destructibles "
              "are not supported");
      std::string issue;
      std::optional<assets::ColliderShapeAsset> geometry;
      if (c["ModelCollider3D"].contains("inline_geometry")) {
        require(c["ModelCollider3D"].value("asset", "").empty(), "Choose asset or inline_geometry, not both");
        geometry = assets::parseColliderShapeAsset(c["ModelCollider3D"]["inline_geometry"], issue);
      } else {
        const auto *manifest = findAsset(registry, c["ModelCollider3D"].value("asset", ""));
        require(manifest && manifest->type == "Collider3D", "Requires a collider asset");
        geometry = assets::loadColliderShapeAsset(manifest->sourcePath, issue);
      }
      require(geometry && geometry->fracture.has_value(),
              "Requires an authored compound fracture graph");
      require(c["Rigidbody3D"].value("body_type", "dynamic") ==
                  (geometry->fracture->anchors.empty() ? "dynamic" : "static"),
              "Anchored assemblies must be static; unanchored assemblies must "
              "be dynamic");
      const auto &config = c["Destructible3D"];
      require(config.is_object() && config.contains("parts") &&
                  config["parts"].is_object() &&
                  config["parts"].size() == geometry->parts.size(),
              "Map every collider part to one visual entity");
      std::set<std::string> used;
      auto visualEntities=entities;
      if(config.contains("deferred_visuals")) {
        require(config["deferred_visuals"].is_object(),"Deferred visuals must be an object");
        for(const auto &[region,items]:config["deferred_visuals"].items()) {
          require(entities.contains(region) && items.is_array() && !items.empty(),"Invalid deferred visual region");
          const auto &regionComponents=entities.at(region)->at("components");
          require(regionComponents.contains("MeshRenderer") && regionComponents.contains("Transform3D") &&
              regionComponents["Transform3D"].value("parent","")==id,"Deferred region must be a direct renderer child");
          for(const auto &item:items) {
            std::string error;
            require(bool(runtime::RuntimeObjectModel::buildEntity(item,error)),"Invalid deferred entity components");
            require(visualEntities.emplace(item.at("id").get<std::string>(),&item).second,"Duplicate deferred visual identity");
          }
        }
      }
      for (const auto &part : geometry->parts) {
        require(config["parts"].contains(part.id) &&
                    config["parts"][part.id].is_string(),
                "Missing collider part visual mapping");
        const auto visualId = config["parts"][part.id].get<std::string>();
        require(visualId != id && used.insert(visualId).second &&
                    visualEntities.contains(visualId),
                "Visual IDs must be distinct existing children");
        require(!visualEntities.at(visualId)->value("persistent", false),
                "Persistent fracture visuals are not supported");
        const auto &visual = visualEntities.at(visualId)->at("components");
        require(
            visual.contains("Transform3D") &&
                visual["Transform3D"].value("parent", "") == id &&
                visual.contains("MeshRenderer") &&
                !visual.contains("Rigidbody3D"),
            "Visuals must be direct renderer children without rigid bodies");
        for (const auto *name :
             {"BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D",
              "ConvexCollider3D", "ModelCollider3D"})
          require(!visual.contains(name),
                  "Fracture visuals cannot own colliders");
      }
    } catch (const std::exception &error) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "DESTRUCTIBLE3D_INVALID",
                             .message = "Entity " + id + ": " + error.what(),
                             .path = path.string()});
    }
  }
}
} // namespace demi
