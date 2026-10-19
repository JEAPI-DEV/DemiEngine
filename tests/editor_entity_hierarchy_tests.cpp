#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PrefabPlacement3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "editor/EditorHierarchyIndex.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <nlohmann/json.hpp>
#include <unordered_set>

using namespace demi::editor;
using namespace demi::runtime;
namespace {
Entity entity(std::string id, std::string parent = {}) {
  Entity result;
  result.id = id;
  result.name = id;
  result.setComponent(Transform3DComponent{.parent = std::move(parent)});
  return result;
}

void largeFlatHierarchyUsesIndexedRelationships() {
  World world;
  constexpr int Children = 30000;
  world.entities.reserve(Children + 1);
  world.entities.push_back(entity("root"));
  for (int index = 0; index < Children; ++index) {
    world.entities.push_back(entity("child-" + std::to_string(index), "root"));
  }
  world.entities.back().name = "Needle";
  world.entities.back().enabled = false;
  const auto authored = nlohmann::json::parse(R"({"entities":[
    {"id":"root","children":[{"id":"child-0"},{"id":"child-29999"}]}
  ]})");
  EditorHierarchyIndex hierarchy(world, authored, "needle");
  assert(hierarchy.size() == Children + 1);
  assert(hierarchy.roots().size() == 1);
  assert(hierarchy.node(hierarchy.roots().front()).children.size() == Children);
  assert(hierarchy.find("root")->visible);
  assert(!hierarchy.find("child-0")->visible);
  assert(hierarchy.find("child-29999")->visible);
  assert(!hierarchy.find("child-29999")->entity->enabled);
  assert(hierarchy.find("child-0")->authored);
  assert(!hierarchy.find("child-1")->authored);
  assert(hierarchy.find("child-29999")->authored);
  // Every row and ancestry lookup consumes the built index; no source scans
  // are performed while asking for children or parents.
  std::size_t edges = 0;
  for (int index = 0; index < Children; ++index) {
    const auto *node = hierarchy.find("child-" + std::to_string(index));
    assert(node && node->parent == hierarchy.roots().front());
    edges += node->children.size();
  }
  assert(edges == 0 && hierarchy.size() == Children + 1);
  hierarchy.filter("");
  assert(hierarchy.find("child-0")->visible);
}

void deepMatchesAndMalformedParentsRemainBounded() {
  World world;
  constexpr int Depth = 12000;
  world.entities.reserve(Depth);
  for (int index = 0; index < Depth; ++index) {
    world.entities.push_back(entity(
        std::to_string(index), index == 0 ? "" : std::to_string(index - 1)));
  }
  world.entities.back().name = "Deep match";
  EditorHierarchyIndex hierarchy(world, nlohmann::json::object(), "deep MATCH");
  for (int index = 0; index < Depth; ++index) {
    assert(hierarchy.find(std::to_string(index))->visible);
  }
  assert(hierarchy.roots().size() == 1);
  World malformed;
  malformed.entities = {entity("a", "b"), entity("b", "a"),
                        entity("orphan", "missing"), entity("self", "self")};
  EditorHierarchyIndex safe(malformed, nlohmann::json::object(), "");
  std::vector<std::size_t> pending = safe.roots();
  std::unordered_set<std::size_t> seen;
  while (!pending.empty()) {
    const auto index = pending.back();
    pending.pop_back();
    assert(seen.insert(index).second);
    const auto &children = safe.node(index).children;
    pending.insert(pending.end(), children.begin(), children.end());
  }
  assert(seen.size() == malformed.entities.size());
  assert(!safe.find("orphan")->parent);
}

void placementPreviewVisibilityMatchesAuthoredOwnership() {
  World world;
  world.entities.push_back(entity("placement"));
  world.entities.front().setComponent(PrefabPlacement3DComponent{});
  world.entities.push_back(entity("preview", "placement"));
  world.entities.push_back(entity("authored", "placement"));
  world.entities.back().prefabInstance = "instance";
  const auto source = nlohmann::json::parse(R"({"entities":[
    {"id":"placement","children":[{"id":"authored"}]}
  ]})");
  EditorHierarchyIndex hierarchy(world, source, "");
  assert(hierarchy.find("placement")->visible);
  assert(hierarchy.find("preview")->hiddenPreview);
  assert(!hierarchy.find("preview")->visible);
  assert(hierarchy.find("authored")->visible);
  assert(hierarchy.find("authored")->entity->prefabInstance == "instance");

  world.entities.push_back(entity("terrain"));
  world.entities.push_back(entity("terrain/chunk", "terrain"));
  world.entities.back().setComponent(
      terrain_detail::TerrainGeneratedSurface{.owner = "terrain"});
  world.entities.push_back(entity("terrain/child", "terrain/chunk"));
  world.entities.push_back(entity("grid"));
  IsoGridComponent grid;
  grid.cellTextures["3,4"] = "asset://tile";
  world.entities.back().setComponent(grid);
  EditorHierarchyIndex extended(world, source, "Cell 3,4");
  assert(extended.find("terrain/chunk")->hiddenPreview);
  assert(extended.find("terrain/child")->hiddenPreview);
  assert(extended.find("grid")->visible);
  assert(extended.find("grid")->paintedCells.size() == 1);
}
void retainedProjectionInvalidatesByRevisionAndClosesRows() {
  World world;
  world.entities = {entity("parent"), entity("child", "parent"),
                    entity("other")};
  const auto document = nlohmann::json::object();
  EditorHierarchyProjection projection;
  projection.refresh(world, document, 1, 1, "");
  assert(projection.indexBuildCount() == 1);
  assert(projection.rows().size() == 3);
  const auto rowBuilds = projection.rowBuildCount();
  for (int frame = 0; frame < 100; ++frame) {
    projection.refresh(world, document, 1, 1, "");
    assert(projection.rows().size() == 3);
  }
  assert(projection.indexBuildCount() == 1);
  assert(projection.rowBuildCount() == rowBuilds);
  projection.setEntityOpen("parent", false);
  assert(projection.rows().size() == 2);
  projection.setEntityOpen("parent", true);
  assert(projection.rows().size() == 3);
  world.entities[1].name = "Renamed child";
  projection.refresh(world, document, 2, 1, "renamed");
  assert(projection.indexBuildCount() == 2);
  assert(projection.rows().size() == 2);
  world.entities[1].component<Transform3DComponent>()->parent = "other";
  projection.refresh(world, document, 3, 1, "renamed");
  assert(projection.rows().size() == 2);
  assert(projection.index()
             .node(*projection.index().find("child")->parent)
             .entity->id == "other");
  std::swap(world.entities[0], world.entities[2]);
  projection.refresh(world, document, 3, 2, "");
  assert(projection.index().find("other")->entity == &world.entities[0]);
  assert(projection.indexBuildCount() == 4);
}

void retainedLargeProjectionDoesNotRewalkIdleRows() {
  World world;
  constexpr int Count = 24000;
  world.entities.reserve(Count);
  for (int index = 0; index < Count; ++index) {
    world.entities.push_back(entity("entity_" + std::to_string(index)));
  }
  EditorHierarchyProjection projection;
  const auto document = nlohmann::json::object();
  projection.refresh(world, document, 1, 1, "");
  assert(projection.rows().size() == Count);
  for (int frame = 0; frame < 50; ++frame) {
    projection.refresh(world, document, 1, 1, "");
    (void)projection.rows();
  }
  assert(projection.indexBuildCount() == 1);
  assert(projection.rowBuildCount() == 1);
}

} // namespace
int main() {
  retainedProjectionInvalidatesByRevisionAndClosesRows();
  retainedLargeProjectionDoesNotRewalkIdleRows();
  largeFlatHierarchyUsesIndexedRelationships();
  deepMatchesAndMalformedParentsRemainBounded();
  placementPreviewVisibilityMatchesAuthoredOwnership();
}
