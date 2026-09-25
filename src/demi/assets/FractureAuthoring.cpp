#include "demi/assets/FractureAuthoring.h"
#include "demi/assets/FracturePrefab.h"
#include "demi/assets/MasonryGeneration.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include <algorithm>
#include <cmath>
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
                         const J &authoredEntities, const std::string &instancePrefix) {
  std::set<std::string> generatedIds;
  const J entities = expandMasonry(authoredEntities, false, &generatedIds);
  if (!hasFractureAuthoring(entities))
    return entities;
  J output = entities;
  std::map<std::string,J> masonryRegions;
  for(const auto &source:authoredEntities) {
    const auto &c=source.at("components");
    if (c.contains("Masonry3D"))
      masonryRegions.emplace(source.at("id").get<std::string>(),source);
  }
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
    if (options.contains("density") && options["density"].is_null())
      options.erase("density");
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
    // Resolve selected ancestor chains once, not once per source entity.
    // Large procedural regions otherwise repeat the same JSON/string walks N² times.
    std::set<std::string> neededIds{rootId};
    for (const auto &[selected, options] : objects.items()) {
      std::string cursor = instancePrefix.empty() ? selected : instancePrefix + "/" + selected;
      std::set<std::string> visited;
      while (indices.contains(cursor) && visited.insert(cursor).second) {
        neededIds.insert(cursor);
        if (cursor == rootId) break;
        cursor = parent(entities[indices.at(cursor)]);
      }
    }
    J inputs = J::array();
    for (const auto &source : entities) {
      const auto sourceId = source["id"].get<std::string>();
      if (!neededIds.contains(sourceId))
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
    const bool densityBased = std::ranges::any_of(objects, [](const J &object) {
      return object.contains("density");
    });
    const auto authoredBody = rootComponents.value("Rigidbody3D", J::object());
    J settings = {{"generator_version", config.value("generator_version", 1)},
                  {"seed", config.value("seed", std::uint32_t(1))},
                  {"max_bodies", config.value("max_bodies", 64)},
                  {"objects", objects}};
    if (authoredBody.contains("mass") || !densityBased)
      settings["mass"] = authoredBody.value(
          "mass", double(runtime::Rigidbody3DComponent{}.mass));
    const auto generatedData = compileFracturePrefab(project, inputs, settings);
    auto compiled = generatedData["entities"];
    std::map<std::string, std::size_t> sourceLeafCounts;
    for (const auto &generated : compiled) {
      if (generated.at("id") != "body")
        continue;
      for (const auto &[part, visual] :
           generated.at("components").at("Destructible3D").at("parts").items())
        ++sourceLeafCounts[generatedData.at("generation").at("sources")
                               .at(visual.get<std::string>()).get<std::string>()];
    }
    std::map<std::string, std::string> sourceRegions, regionSources;
    std::map<std::string, J> intactSources;
    for (const auto &[sourceLocal, count] : sourceLeafCounts) {
      const auto sourceId = instancePrefix.empty()
                                ? sourceLocal
                                : instancePrefix + "/" + sourceLocal;
      if (count < 2 || generatedIds.contains(sourceId))
        continue;
      const auto region = sourceId == rootId ? rootId + "/intact" : sourceId;
      require(region == sourceId || !indices.contains(region),
              "Intact fracture visual ID collision: " + region);
      sourceRegions[sourceId] = region;
      regionSources[region] = sourceId;
      J intact = entities[indices.at(sourceId)];
      if (sourceId == rootId) {
        intact = {{"id", region},
                  {"components",
                   {{"MeshRenderer", intact["components"]["MeshRenderer"]}}}};
        for (const char *field : {"enabled", "layer", "persistent"})
          if (entities[rootIndex].contains(field))
            intact[field] = entities[rootIndex][field];
      }
      intact["components"].erase("Fracture3D");
      intactSources[region] = std::move(intact);
    }
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
        double mass = c["Rigidbody3D"]["mass"].get<double>();
        if (densityBased && !authoredBody.contains("mass")) {
          const auto scale = rootComponents["Transform3D"].value(
              "scale", J::array({1, 1, 1}));
          mass *= std::abs(scale[0].get<double>() * scale[1].get<double>() *
                           scale[2].get<double>());
          require(std::isfinite(mass) && mass > 0 && mass < 1e12,
                  "Scaled density-derived mass is outside supported limits");
        }
        body["mass"] = mass;
        for (const char *collider :
             {"BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D",
              "ConvexCollider3D", "ModelCollider3D"})
          require(!rootComponents.contains(collider),
                  "Fracture generates its root collider; remove authored " +
                      std::string(collider));
        rootComponents["Rigidbody3D"] = std::move(body);
        rootComponents["ModelCollider3D"] = c["ModelCollider3D"];
        rootComponents["Destructible3D"] = c["Destructible3D"];
        if (config.contains("energy_per_health"))
          rootComponents["Destructible3D"]["energy_per_health"] = config["energy_per_health"];
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
    J deferred=J::object();
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
      const auto region = parent(source);
      if (sourceRegions.contains(sourceId)) {
        const auto &sourceRegion = sourceRegions.at(sourceId);
        if (!deferred.contains(sourceRegion))
          deferred[sourceRegion] = J::array();
        deferred[sourceRegion].push_back(std::move(generated));
      } else if (generatedIds.contains(sourceId) &&
                 masonryRegions.contains(region)) {
        if (!deferred.contains(region))
          deferred[region] = J::array();
        deferred[region].push_back(std::move(generated));
      } else
        output.push_back(std::move(generated));
    }
    if(!deferred.empty()) {
      // Cook one intact regional surface and retain leaf descriptions as cold
      // data. Physics still owns the complete support graph for picking/damage.
      runtime::World localWorld;
      for(const auto &input:inputs) {
        std::string error;
        auto entity=runtime::RuntimeObjectModel::buildEntity(input,error);
        require(bool(entity),error);
        localWorld.entities.push_back(std::move(*entity));
      }
      J intactReferences = J::object();
      for (const auto &[region, templates] : deferred.items()) {
        J intact;
        if (intactSources.contains(region)) {
          intact = intactSources.at(region);
        } else {
          const auto &sourceRegion = masonryRegions.at(region);
          const auto &settings = sourceRegion.at("components").at("Masonry3D");
          const bool usesModels =
              !settings.value("models", J::object()).empty();

          if (usesModels) {
            auto batches = buildIntactMasonryModelBatches(sourceRegion);
            intact = std::move(batches.front());

            for (std::size_t batchIndex = 1; batchIndex < batches.size();
                 ++batchIndex) {
              auto &batch = batches[batchIndex];
              const std::string batchId = batch.at("id").get<std::string>();
              require(!indices.contains(batchId),
                      "Intact masonry batch ID collision: " + batchId);

              if (!intactReferences.contains(region)) {
                intactReferences[region] = J::array();
              }
              intactReferences[region].push_back(batchId);
              output.push_back(std::move(batch));
            }
          } else {
            intact = masonryIntactVisual(sourceRegion);
          }
        }
        const auto source =
            regionSources.contains(region) ? regionSources.at(region) : region;
        const auto local = std::ranges::find(
            localWorld.entities, localId(source), &runtime::Entity::id);
        require(local != localWorld.entities.end(),
                "Missing deferred source transform");
        const auto pose = runtime::resolveWorldTransform3D(localWorld, *local);
        require(bool(pose), "Invalid deferred source transform");
        intact["components"]["Transform3D"] = {
            {"parent", rootId},
            {"position",
             {pose->position.x, pose->position.y, pose->position.z}},
            {"rotation",
             {pose->rotation.x, pose->rotation.y, pose->rotation.z}},
            {"scale", {pose->scale.x, pose->scale.y, pose->scale.z}}};
        if (indices.contains(region))
          output[indices.at(region)] = std::move(intact);
        else
          output.push_back(std::move(intact));
      }
      output[rootIndex]["components"]["Destructible3D"]["deferred_visuals"] =
          std::move(deferred);
      if (!intactReferences.empty())
        output[rootIndex]["components"]["Destructible3D"]["intact_visuals"] =
            std::move(intactReferences);
    }
  }
  std::erase_if(output.get_ref<J::array_t &>(), [&](const J &entity) {
    return generatedIds.contains(entity.at("id").get<std::string>()) &&
           !entity["components"].contains("MeshRenderer");
  });
  return output;
}
} // namespace demi::assets
