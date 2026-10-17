#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainScatterCollision.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "editor/EditorSceneJson.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>

namespace demi::editor {
namespace {

std::optional<nlohmann::json>
promotionPrefabOverrides(const std::filesystem::path &scenePath,
                         const runtime::TerrainScatterPlacement &placement,
                         std::string &error) {
  using Json = nlohmann::json;
  const Json probe{{"format_version", 1},
                   {"entities", Json::array({{{"id", "promotion"},
                                              {"prefab", placement.prefab}}})}};
  const auto expanded =
      runtime::composition::expandSceneForAuthoring(scenePath, probe);
  if (!expanded.document) {
    error = "The selected placement's prefab could not be resolved.";
    return std::nullopt;
  }
  Json overrides = Json::object();
  for (const auto &source : expanded.document->at("entities")) {
    const auto id = source.at("id").get<std::string>();
    if (!id.starts_with("promotion/")) {
      error = "The prefab expanded outside its instance identity.";
      return std::nullopt;
    }
    const auto localId = id.substr(std::string_view("promotion/").size());
    const auto components =
        runtime::terrainScatterCollisionOverrides(source, placement.collision);
    if (!components.empty()) {
      overrides[localId]["components"] = components;
    }
  }
  return overrides;
}

} // namespace

bool EditorSceneDocument::promoteTerrainScatter(SceneValueTarget terrainTarget,
                                                std::string placementId,
                                                nlohmann::json authoredObject,
                                                std::string &error) {
  if (placementId.empty() || !authoredObject.is_object()) {
    error = "Promotion requires a stable placement ID and an authored object.";
    return false;
  }
  const auto components = authoredObject.find("components");
  if (components == authoredObject.end() || !components->is_object() ||
      !components->contains("Transform3D") ||
      !components->at("Transform3D").is_object()) {
    error = "A promoted placement requires an authored Transform3D object.";
    return false;
  }
  if (const auto children = authoredObject.find("children");
      children != authoredObject.end() &&
      (!children->is_array() || children->size() != 1 ||
       !(*children)[0].is_object())) {
    error = "A promoted prefab requires one authored child instance.";
    return false;
  }
  const auto expanded =
      runtime::composition::expandSceneForAuthoring(path_, document_);
  const auto *owner =
      expanded.document ? findEntity(*expanded.document, terrainTarget.entityId)
                        : nullptr;
  if (!owner || !findComponent(*owner, "Terrain3D")) {
    error = "Select an authored terrain owner before promoting a placement.";
    return false;
  }
  auto staged = document_;
  terrainTarget.component = "Terrain3D";
  terrainTarget.field = "scatter_exclusions";
  const auto *previous = valueInDocument(staged, terrainTarget);
  auto exclusions = previous ? *previous : nlohmann::json::array();
  if (!exclusions.is_array() || std::find(exclusions.begin(), exclusions.end(),
                                          placementId) != exclusions.end()) {
    error = "This scatter placement has already been promoted or excluded.";
    return false;
  }
  exclusions.push_back(std::move(placementId));
  if (!assignValueInDocument(staged, terrainTarget, exclusions)) {
    error = "The terrain's authored component could not be found.";
    return false;
  }
  const auto id = uniqueEntityId(staged, "terrain_detail");
  authoredObject["id"] = id;
  if (authoredObject.contains("children")) {
    authoredObject["children"][0]["id"] =
        uniqueEntityId(staged, id + "_content");
  }
  if (!terrainTarget.isPrefabOverride()) {
    auto *authoredOwner = findEntity(staged, terrainTarget.entityId);
    if (!authoredOwner) {
      error = "The terrain requires a resolved prefab override target.";
      return false;
    }
    auto &children = (*authoredOwner)["children"];
    if (children.is_null()) {
      children = nlohmann::json::array();
    }
    authoredObject["components"]["Transform3D"].erase("parent");
    children.push_back(std::move(authoredObject));
  } else {
    // Cross-prefab parenting is explicit; the prefab source remains untouched.
    staged["entities"].push_back(std::move(authoredObject));
  }
  EntityHierarchyCommand command{.entityId = id,
                                 .before = document_["entities"],
                                 .after = staged["entities"]};
  if (document_.contains("instances")) {
    command.instancesBefore = document_["instances"];
    command.instancesAfter = staged["instances"];
  }
  // Ownership transfer and procedural suppression are one undoable operation.
  return stageAndCommit(std::move(command), error);
}

bool EditorWorkspace::promoteTerrainScatter(std::string_view ownerId,
                                            std::string_view placementId,
                                            std::string &error) {
  if (!project_ || activeDocument_ != EditorWorkspaceDocument::Scene) {
    error = "Open a scene to promote a terrain placement.";
    return false;
  }
  const auto *owner =
      runtime::findEntity(project_->world, std::string(ownerId));
  const auto *terrain =
      owner ? owner->component<runtime::Terrain3DComponent>() : nullptr;
  if (!terrain || !terrain->generated) {
    error = "Generate the terrain before promoting its placements.";
    return false;
  }
  const auto &field = *terrain->generated;
  const auto found = std::find_if(
      field.scatterPlacements.begin(), field.scatterPlacements.end(),
      [&](const auto &placement) {
        return runtime::terrainScatterInstanceId(
                   ownerId, field.paletteId, placement.ruleId,
                   placement.candidateX, placement.candidateZ) == placementId;
      });
  if (found == field.scatterPlacements.end()) {
    error = "This placement no longer exists. Refresh the terrain selection.";
    return false;
  }
  const auto &placement = *found;
  nlohmann::json object{
      {"name", placement.ruleId},
      {"components",
       {{"Transform3D",
         {{"parent", ownerId},
          {"position",
           {placement.position.x, placement.position.y, placement.position.z}},
          {"rotation", {0, placement.yaw, 0}},
          {"scale", {placement.scale, placement.scale, placement.scale}}}}}}};
  if (placement.prefab.empty()) {
    object["components"]["MeshRenderer"] = {{"model", placement.model}};
  } else {
    object["children"] =
        nlohmann::json::array({{{"prefab", placement.prefab}}});
    const auto overrides =
        promotionPrefabOverrides(sceneDocument_.path(), placement, error);
    if (!overrides) {
      return false;
    }
    if (!overrides->empty()) {
      object["children"][0]["overrides"] = *overrides;
    }
  }
  auto target = resolveSceneTarget({.entityId = std::string(ownerId)});
  const bool changed = mutateAndRebuild(
      [&](EditorSceneDocument &document, std::string &failure) {
        return document.promoteTerrainScatter(target, std::string(placementId),
                                              object, failure);
      },
      error);
  if (changed) {
    selectEntity(std::string(sceneDocument_.lastChangedEntityId()));
  }
  return changed;
}

} // namespace demi::editor
