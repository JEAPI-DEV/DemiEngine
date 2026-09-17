#include "demi/assets/FractureAuthoring.h"
#include "demi/assets/FracturePrefab.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include <map>
#include <set>
#include <stdexcept>

namespace demi::assets {
namespace {
using J = nlohmann::json;
std::string parent(const J &entity) {
  const auto &c = entity.at("components");
  return c.contains("Transform3D") ? c["Transform3D"].value("parent", "") : "";
}
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool authoringRoot(const J &c) {
  if (!c.contains("Destructible3D")) return false;
  const auto &config = c["Destructible3D"];
  return !config.is_object() || !config.contains("parts") ||
         !config["parts"].is_object() || config["parts"].empty();
}
} // namespace

bool hasFractureAuthoring(const J &value) {
  if (value.is_object()) {
    if (value.contains("components") && value["components"].is_object()) {
      const auto &c = value["components"];
      if (c.contains("Fracture3D") || authoringRoot(c))
        return true;
    }
    for (const auto &[key, child] : value.items())
      if ((key == "entities" || key == "children") &&
          hasFractureAuthoring(child))
        return true;
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (hasFractureAuthoring(child))
        return true;
  }
  return false;
}

J compileEntityFractures(const std::filesystem::path &project,
                         const J &entities, const std::string &instancePrefix) {
  if (!hasFractureAuthoring(entities))
    return entities;
  J output = entities;
  std::map<std::string, std::size_t> indices;
  std::map<std::string, J> selections;
  const auto localId = [&](const std::string &id) {
    const auto prefix = instancePrefix + "/";
    return !instancePrefix.empty() && id.starts_with(prefix)
               ? id.substr(prefix.size())
               : id;
  };
  for (std::size_t i = 0; i < entities.size(); ++i) {
    const auto &entity = entities[i];
    require(indices.emplace(entity.at("id").get<std::string>(), i).second,
            "Duplicate fracture entity ID");
    if (!entity.contains("components"))
      continue;
    const auto &c = entity["components"];
    if (c.contains("Fracture3D") || c.contains("Destructible3D")) {
      std::string error;
      require(bool(runtime::RuntimeObjectModel::buildEntity(entity, error)),
              error);
    }
    if (authoringRoot(c))
      selections.emplace(entity["id"].get<std::string>(), J::object());
  }
  for (const auto &entity : entities) {
    if (!entity.contains("components") ||
        !entity["components"].contains("Fracture3D"))
      continue;
    const auto id = entity["id"].get<std::string>();
    std::string owner = id;
    std::set<std::string> visited;
    while (!owner.empty() && !selections.contains(owner)) {
      require(visited.insert(owner).second && indices.contains(owner),
              "Invalid fracture ownership hierarchy: " + id);
      const auto &candidate = entities[indices.at(owner)];
      require(!candidate["components"].contains("Destructible3D"),
              "Cannot add Fracture3D below a prepared parts mapping");
      owner = parent(candidate);
    }
    require(!owner.empty(),
            "Fracture3D requires Destructible3D on itself or an ancestor: " +
                id);
    auto options = entity["components"]["Fracture3D"];
    if (options.contains("anchor_below") && options["anchor_below"].is_null())
      options.erase("anchor_below");
    if (options.value("interior_material", "").empty())
      options.erase("interior_material");
    selections[owner][localId(id)] = std::move(options);
  }
  for (const auto &[rootId, objects] : selections) {
    const auto rootIndex = indices.at(rootId);
    auto &root = output[rootIndex];
    auto &rootComponents = root["components"];
    const auto config = rootComponents["Destructible3D"];
    if (objects.empty()) {
      // Allow adding the assembly component before opting in its first mesh.
      rootComponents.erase("Destructible3D");
      continue;
    }
    require(parent(root).empty(),
            "Fracture assembly roots must be unparented; nested physical "
            "assemblies are not supported yet: " +
                rootId);
    require(rootComponents.contains("Transform3D"),
            "Fracture assembly requires Transform3D: " + rootId);
    J inputs = J::array();
    for (const auto &source : entities) {
      const auto sourceId = source["id"].get<std::string>();
      // Include selected geometry and its transform ancestors only.
      bool needed = sourceId == rootId;
      for (const auto &[selected, options] : objects.items()) {
        std::string cursor =
            instancePrefix.empty() ? selected : instancePrefix + "/" + selected;
        std::set<std::string> visited;
        while (indices.contains(cursor) && visited.insert(cursor).second) {
          needed |= cursor == sourceId;
          if (cursor == rootId)
            break;
          cursor = parent(entities[indices.at(cursor)]);
        }
      }
      if (!needed)
        continue;
      auto input = source;
      input["id"] = localId(sourceId);
      auto &c = input["components"];
      c.erase("Destructible3D");
      c.erase("Fracture3D");
      if (sourceId == rootId)
        c["Transform3D"] = J::object();
      else if (c.contains("Transform3D") && c["Transform3D"].contains("parent"))
        c["Transform3D"]["parent"] =
            localId(c["Transform3D"]["parent"].get<std::string>());
      inputs.push_back(std::move(input));
    }
    J settings = {{"generator_version", config.value("generator_version", 1)},
                  {"seed", config.value("seed", std::uint32_t(1))},
                  {"max_bodies", config.value("max_bodies", 64)},
                  {"objects", objects},
                  {"mass", rootComponents.value("Rigidbody3D", J::object())
                      .value("mass", double(runtime::Rigidbody3DComponent{}.mass))}};
    const auto generatedData = compileFracturePrefab(project, inputs, settings);
    auto compiled = generatedData["entities"];
    std::map<std::string, std::string> remap{{"body", rootId}};
    for (const auto &generated : compiled) {
      const auto id = generated["id"].get<std::string>();
      if (id == "body")
        continue;
      const auto generatedId = rootId + "/shards/" + id;
      require(!indices.contains(generatedId),
              "Generated fracture ID conflicts with source: " + generatedId);
      remap.emplace(id, generatedId);
    }
    for (auto &generated : compiled) {
      auto &c = generated["components"];
      const auto id = generated["id"].get<std::string>();
      if (id == "body") {
        for (auto &[part, visual] : c["Destructible3D"]["parts"].items())
          visual = remap.at(visual.get<std::string>());
        // Explicit body properties survive generation. Body type follows
        // support.
        auto body = rootComponents.value("Rigidbody3D", J::object());
        const auto generatedType = c["Rigidbody3D"]["body_type"];
        require(body.value("body_type", "dynamic") != "kinematic",
                "Kinematic fracture assemblies are not supported: " + rootId);
        body["body_type"] = generatedType;
        body["mass"] = settings["mass"];
        for (const char *collider :
             {"BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D",
              "ConvexCollider3D", "ModelCollider3D"})
          require(!rootComponents.contains(collider),
                  "Fracture generates its root collider; remove authored " +
                      std::string(collider));
        rootComponents["Rigidbody3D"] = std::move(body);
        rootComponents["ModelCollider3D"] = c["ModelCollider3D"];
        rootComponents["Destructible3D"] = c["Destructible3D"];
      }
    }
    for (auto &source : output) {
      const auto id = source["id"].get<std::string>();
      if (!objects.contains(localId(id)))
        continue;
      auto &c = source["components"];
      if (id != rootId) {
        require(!c.contains("Rigidbody3D"),
                "Fracture child cannot own a separate Rigidbody3D: " + id);
        for (const char *collider :
             {"BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D",
              "ConvexCollider3D", "ModelCollider3D"})
          require(!c.contains(collider),
                  "Fracture child colliders are generated; remove " +
                      std::string(collider));
      }
      c.erase("MeshRenderer");
      c.erase("Fracture3D");
    }
    // Append only after finishing references into the output array.
    for (auto generated : compiled) {
      const auto id = generated["id"].get<std::string>();
      if (id == "body")
        continue;
      generated["id"] = remap.at(id);
      auto &c = generated["components"];
      c["Transform3D"]["parent"] =
          remap.at(c["Transform3D"]["parent"].get<std::string>());
      for (const char *field : {"enabled", "layer", "persistent"})
        if (entities[rootIndex].contains(field))
          generated[field] = entities[rootIndex][field];
      const auto sourceLocal =
          generatedData["generation"]["sources"][id].get<std::string>();
      const auto sourceId = instancePrefix.empty()
                                ? sourceLocal
                                : instancePrefix + "/" + sourceLocal;
      const auto &source = entities[indices.at(sourceId)];
      if (source.contains("layer"))
        generated["layer"] = source["layer"];
      if (!source.value("enabled", true))
        generated["enabled"] = false;
      output.push_back(std::move(generated));
    }
  }
  return output;
}
} // namespace demi::assets
