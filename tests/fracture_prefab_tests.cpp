#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/ConvexFracture.h"
#include "demi/assets/FractureAuthoring.h"
#include "demi/assets/MasonryGeneration.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"
#include "editor/EditorWorkspace.h"
#include "editor/EditorSpecializedDocument.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
using J = nlohmann::json;
using namespace demi;
using namespace demi::runtime;
namespace {
void check(bool value, const std::string &error) {
  if (!value)
    throw std::runtime_error(error);
}
void write(const std::filesystem::path &p, const J &j) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p);
  f << j.dump(2);
  check(bool(f), "Fixture write failed");
}
J source() {
  return J::parse(R"({"format_version":1,"id":"prefab://source","entities":[
  {"id":"wall","components":{"Transform3D":{"position":[0,2,0]},"MeshRenderer":{"shape":"cube","size":[4,4,0.4],"color":[0.5,0.5,0.5,1]}}}
]})");
}
J fracturePrefab() {
  auto mesh = source()["entities"][0];
  mesh["components"]["Fracture3D"] = {{"pieces", 12}, {"anchor_below", 0.001}};
  return {{"format_version", 1},
          {"id", "prefab://broken"},
          {"entities", {{{"id", "body"},
                          {"components", {{"Transform3D", J::object()},
                                          {"Destructible3D", {{"seed", 123}}}}},
                          {"children", J::array({mesh})}}}}};
}

void obsoleteRecipeRejected(const std::filesystem::path &root) {
  write(root / "demi.project.json",
        {{"format_version", 1},
         {"name", "Removed fracture format"},
         {"main_scene", "scene://test/main"},
         {"scenes", {{{"id", "scene://test/main"}}}}});
  write(root / "scenes/main.scene.json",
        {{"format_version", 1}, {"id", "scene://test/main"},
         {"entities", J::array()}});
  const auto path = root / "prefabs/legacy.prefab.json";
  const J legacy = {{"format_version", 1},
                    {"id", "prefab://legacy"},
                    {"fracture", {{"generator_version", 1},
                                  {"source", "prefab://missing"},
                                  {"objects", {{"wall", {{"pieces", 2}}}}}}}};
  for (const bool withEntities : {false, true}) {
    auto document = legacy;
    if (withEntities)
      document["entities"] = J::array();
    write(path, document);
    check(hasErrors(validatePath(path).diagnostics),
          "Prefab schema accepted a removed top-level fracture recipe");
    const auto expanded = composition::expandPrefabInstance(
        path, {{"id", "instance"}, {"prefab", "prefab://legacy"}});
    check(!expanded.document && hasErrors(expanded.diagnostics),
          "Runtime accepted a removed top-level fracture recipe");
    check(expanded.diagnostics.front().code == "PREFAB_INVALID_DOCUMENT",
          "Runtime tried to resolve the removed recipe's source");
    check(!composition::preparePrefabDocument(path, document).document,
          "Cooking accepted a removed top-level fracture recipe");
    check(!composition::bakeFracturePrefab(path).document,
          "Fracture command accepted a removed top-level recipe");
    const auto editorDiagnostics = editor::validateSpecializedDocument(
        editor::EditorSpecializedKind::Prefab, path, document);
    check(hasErrors(editorDiagnostics) &&
              editorDiagnostics.front().code == "PREFAB_INVALID_DOCUMENT",
          "Editor validation accepted a removed top-level recipe");
    editor::EditorSceneDocument editorDocument;
    std::string error;
    check(!editorDocument.open(path, error),
          "Editor opened a removed top-level recipe as an editable prefab");
    const auto cooked = root / "build/cooked";
    const auto cookDiagnostics = assets::cookProject(
        {.projectFile = root / "demi.project.json",
         .outputDirectory = cooked,
         .platform = "linux"});
    const bool rejectedRecipe = std::ranges::any_of(
        cookDiagnostics, [&](const Diagnostic &diagnostic) {
          return diagnostic.severity == Severity::Error &&
                 diagnostic.code == "PREFAB_INVALID_DOCUMENT" &&
                 diagnostic.path == path.string();
        });
    check(rejectedRecipe,
          "Cook did not validate the unreferenced legacy prefab");
    check(!std::filesystem::exists(cooked / "prefabs/legacy.prefab.json"),
          "Cook copied a removed top-level recipe into build output");
  }
  const J prepared = {{"format_version", 1},
                      {"id", "prefab://legacy"},
                      {"entities", J::array()},
                      {"source_recipe", legacy}};
  write(path, prepared);
  const auto expanded = composition::expandPrefabInstance(
      path, {{"id", "instance"}, {"prefab", "prefab://legacy"}});
  check(!expanded.document && !expanded.diagnostics.empty() &&
            expanded.diagnostics.front().code == "PREFAB_SOURCE_RECIPE_INVALID",
        "Cooked source_recipe accepted the removed public recipe format");
}
void writeTetraGlb(const std::filesystem::path &path, bool closed) {
  J json = J::parse(
      R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"byteLength":72}],
    "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":24}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
                 {"bufferView":1,"componentType":5123,"count":12,"type":"SCALAR"}]})");
  if (!closed)
    json["accessors"][1]["count"] = 9;
  std::string text = json.dump();
  while (text.size() % 4)
    text += ' ';
  std::ofstream out(path, std::ios::binary);
  const auto word = [&](std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
      out.put(static_cast<char>(value >> (8 * i)));
  };
  word(0x46546c67);
  word(2);
  word(12 + 8 + text.size() + 8 + 72);
  word(text.size());
  word(0x4e4f534a);
  out.write(text.data(), text.size());
  word(72);
  word(0x004e4942);
  for (float value :
       {0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F})
    word(std::bit_cast<std::uint32_t>(value));
  for (unsigned index : {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3}) {
    out.put(char(index));
    out.put(0);
  }
  check(bool(out), "GLB fixture write failed");
}
void importedModel(const std::filesystem::path &root) {
  writeTetraGlb(root / "tetra.glb", true);
  const auto imported = assets::importAsset({.projectDirectory = root,
                                             .source = root / "tetra.glb",
                                             .id = "asset://tetra"});
  check(!hasErrors(imported.diagnostics), "GLB fixture import failed");
  auto config = fracturePrefab();
  auto &components = config["entities"][0]["children"][0]["components"];
  auto &renderer = components["MeshRenderer"];
  renderer["model"] = "asset://tetra";
  renderer["size"] = {1, 1, 1};
  components["Fracture3D"]["pieces"] = 4;
  write(root / "prefabs/broken.prefab.json", config);
  const auto generated =
      composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json");
  if (!generated.document) {
    std::string error;
    for (const auto &d : generated.diagnostics)
      error += d.message + "\n";
    throw std::runtime_error(error);
  }
  check((*generated.document)["entities"][0]["components"]["ModelCollider3D"]
                             ["inline_geometry"]["parts"]
                                 .size() == 4,
        "Imported GLB was not split into requested geometry");
  const auto manifest = loadAssetManifest(imported.manifestPath);
  writeTetraGlb(manifest->sourcePath, false);
  check(!composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
             .document,
        "Open imported mesh was silently approximated");
}

void lazyImportedSources(const std::filesystem::path &root) {
  std::filesystem::create_directories(root);
  writeTetraGlb(root / "tetra.glb", true);
  const auto imported = assets::importAsset({.projectDirectory = root,
                                             .source = root / "tetra.glb",
                                             .id = "asset://tetra"});
  check(!hasErrors(imported.diagnostics), "Lazy source import failed");
  write(root / "prefabs/solid.prefab.json",
        J::parse(R"({"format_version":1,"id":"prefab://solid","entities":[
    {"id":"solid","components":{"Transform3D":{"position":[0,2,0]},
    "MeshRenderer":{"model":"asset://tetra","size":[1,1,1],"color":[0.5,0.5,0.5,1]},"Destructible3D":{},
    "Fracture3D":{"pieces":4,"anchor_below":0.001}}}]})"));
  write(root / "prefabs/nested.prefab.json",
        J::parse(R"({"format_version":1,"id":"prefab://nested","entities":[
    {"id":"n","prefab":"prefab://solid","overrides":{"solid.Transform3D.position":[5,2,0]}}]})"));
  write(root / "demi.project.json",
        {{"format_version", 1},
         {"name", "Lazy models"},
         {"main_scene", "scene://test/main"},
         {"scenes", {{{"id", "scene://test/main"}}}}});
  write(
      root / "scenes/main.scene.json",
      J::parse(
          R"({"format_version":1,"id":"scene://test/main","entities":[],"instances":[
    {"id":"a","prefab":"prefab://solid"},{"id":"b","prefab":"prefab://nested",
     "overrides":{"n/solid.Fracture3D.pieces":6}}]})"));
  std::string error;
  auto loaded = loadProject(root / "demi.project.json", error);
  check(bool(loaded), error);
  auto &world = loaded->world;
  check(world.entities.size() == 4,
        "Intact imported sources allocated live shards");
  check(findEntity(world, "a/solid/intact")
                ->component<MeshRendererComponent>()
                ->model == "asset://tetra",
        "Intact source lost its model");
  check(findEntity(world, "b/n/solid")
                ->component<Transform3DComponent>()
                ->position.x == 5,
        "Nested source override was lost");
  check(findEntity(world, "b/n/solid")
                ->component<Destructible3DComponent>()->parts.size() == 6,
        "Outer fracture override was applied after nested compilation");
  const auto mapping =
      findEntity(world, "a/solid")->component<Destructible3DComponent>()->parts;
  const auto templates = *findEntity(world, "a/solid")
                              ->component<Destructible3DComponent>()
                              ->deferredVisuals;
  check(templates.at("a/solid/intact").size() > mapping.size(),
        "Interior surfaces are not in the deferred templates");
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.F / 60, {});
  check(world.destruction3D->state("a/solid").status == "ready",
        world.destruction3D->state("a/solid").error);
  findEntity(world, "a/solid")
      ->component<Destructible3DComponent>()
      ->maxBodies = 1;
  check(world.destruction3D->damagePart("a/solid", mapping.begin()->first, 100,
                                        error),
        error);
  physics.step(world, 1.F / 60, {});
  check(world.destruction3D->state("a/solid").status == "failed" &&
            world.entities.size() == 4,
        "Rejected source split leaked shard entities");
  findEntity(world, "a/solid")
      ->component<Destructible3DComponent>()
      ->maxBodies = 64;
  check(world.destruction3D->damagePart("a/solid", mapping.begin()->first, 100,
                                        error),
        error);
  physics.step(world, 1.F / 60, {});
  check(world.destruction3D->state("a/solid").status == "applied",
        world.destruction3D->state("a/solid").error);
  check(!findEntity(world, "a/solid/intact")
             ->hasComponent<MeshRendererComponent>(),
        "Broken source was still drawn intact");
  check(findEntity(world, "b/n/solid/intact")
            ->hasComponent<MeshRendererComponent>(),
        "Untouched nested source was expanded");
  for (const auto &item : templates.at("a/solid/intact")) {
    const auto *visual = findEntity(world, item.at("id").get<std::string>());
    check(visual && visual->hasComponent<MeshRendererComponent>(),
          "Missing exterior/interior shard surface");
    const auto originalParent =
        item.at("components").at("Transform3D").at("parent").get<std::string>();
    if (originalParent != "a/solid")
      check(visual->component<Transform3DComponent>()->parent == originalParent,
            "Interior surface lost its owning shard");
  }
  const auto saved = world.destruction3D->checkpoint(world, "a/solid", error);
  check(!saved.is_null(), error);
  auto restored = loadProject(root / "demi.project.json", error);
  check(bool(restored), error);
  auto &native = ensurePhysicsWorld3D(restored->world);
  native.step(restored->world, 1.F / 60, {});
  check(restored->world.destruction3D->restore("a/solid", saved, error), error);
  native.step(restored->world, 1.F / 60, {});
  check(restored->world.destruction3D->state("a/solid").status == "applied",
        restored->world.destruction3D->state("a/solid").error);
  check(findEntity(restored->world, "b/n/solid/intact")
            ->hasComponent<MeshRendererComponent>(),
        "Restore expanded unrelated source");
  const auto cooked = root / "build/cooked";
  check(
      !hasErrors(assets::cookProject({.projectFile = root / "demi.project.json",
                                      .outputDirectory = cooked,
                                      .platform = "linux"})),
      "Lazy model cook failed");
  auto shipping = loadProject(cooked / "demi.project.json", error);
  check(bool(shipping), error);
  check(shipping->world.entities.size() == 4,
        "Cooked lazy models expanded eagerly");
  check(findEntity(shipping->world, "b/n/solid")
                ->component<Destructible3DComponent>()->parts.size() == 6,
        "Cooking lost the outer fracture override");

  RuntimePrefabService prefabs;
  prefabs.configure(cooked);
  WorldCommandBuffer pending;
  const auto instance = prefabs.instantiate(
      shipping->world, pending, "prefab://nested",
      {.id = "changed", .overrides = {{"n/solid.Fracture3D.pieces", 8}}});
  check(bool(instance), "Cooked runtime fracture override failed");
  check(pending.pendingEntity("changed/n/solid")
                ->component<Destructible3DComponent>()->parts.size() == 8,
        "Cooked runtime override used stale prepared geometry");
}

void componentAuthoring(const std::filesystem::path &root) {
  const auto prefab =
      J::parse(R"({"format_version":1,"id":"prefab://wall","entities":[
    {"id":"wall","components":{"Transform3D":{"position":[3,0,0]},"Destructible3D":{"energy_per_health":2500},
      "Rigidbody3D":{"body_type":"static","mass":50,"friction":0.7}},"children":[
      {"id":"block","components":{"Transform3D":{"position":[0,2,0]},
       "GameplayData":{"values":{"preserved":true}},
       "MeshRenderer":{"shape":"cube","size":[2,4,0.4]},
       "Fracture3D":{"pieces":6,"anchor_below":0.001}}}
    ]}
  ]})");
  const auto scene = J::parse(R"({"format_version":1,"id":"scene://test/main","entities":[],"instances":[
    {"id":"a","prefab":"prefab://wall"},
    {"id":"b","prefab":"prefab://wall","overrides":{"wall.Transform3D.position":[8,0,0]}}
  ]})");
  write(root / "demi.project.json", {{"format_version",1},{"name","Component fracture test"},
    {"main_scene","scene://test/main"},{"scenes",{{{"id","scene://test/main"},{"path","scenes/main.scene.json"}},
      {{"id","scene://test/direct"},{"path","scenes/direct.scene.json"}}}}});
  write(root / "prefabs/wall.prefab.json", prefab);
  write(root / "scenes/main.scene.json", scene);
  auto direct = prefab; direct["id"] = "scene://test/direct";
  write(root / "scenes/direct.scene.json", direct);
  const auto baked = composition::bakeFracturePrefab(root / "prefabs/wall.prefab.json");
  if (!baked.document) {
    std::string issue;
    for (const auto &d : baked.diagnostics) issue += d.message + "\n";
    throw std::runtime_error(issue);
  }
  check(!assets::hasFractureAuthoring(*baked.document), "Cook retained authoring components");
  const auto &c = (*baked.document)["entities"][0]["components"];
  check(c["Transform3D"]["position"] == J({3,0,0}) && c["Rigidbody3D"]["mass"] == 50,
        "Root placement or authored mass lost");
  check(c["ModelCollider3D"]["inline_geometry"]["parts"].size() == 6,
        "Component piece count ignored");
  check(c["Destructible3D"]["energy_per_health"] == 2500,
        "Fracture compilation lost the authored energy scale");
  check((*baked.document)["entities"][1]["id"] == "block" &&
        (*baked.document)["entities"][1]["components"].contains("GameplayData") &&
        (*baked.document)["entities"][1]["components"].contains("MeshRenderer") &&
        (*baked.document)["entities"].size()==2,
        "Intact source identity/behavior lost or live shards were created");
  std::string error;
  auto loaded = loadProject(root / "demi.project.json", error);
  check(bool(loaded), error);
  auto &world = loaded->world;
  const auto *a = findEntity(world, "a/wall");
  const auto *b = findEntity(world, "b/wall");
  check(a && b && a->component<ModelCollider3DComponent>()->inlineGeometry->revision ==
                     b->component<ModelCollider3DComponent>()->inlineGeometry->revision,
        "Instance ID/placement changed generated geometry");
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F/60);
  auto state = world.destruction3D->state("a/wall");
  check(state.status == "ready", state.error);
  check(world.destruction3D->damagePart("a/wall", state.parts.begin()->first, 2, error), error);
  physics.step(world, 1.0F/60);
  check(world.destruction3D->state("a/wall").revision == 1 && world.destruction3D->state("b/wall").revision == 0,
        "Component instances shared damage or failed to split");
  check(!findEntity(world,"a/block")->hasComponent<MeshRendererComponent>() &&
        findEntity(world,"b/block")->hasComponent<MeshRendererComponent>(),"Source visual transition affected the wrong instance");
  {
    editor::EditorWorkspace workspace;
    check(workspace.open(root / "demi.project.json", error), error);
    check(findEntity(workspace.project().world, "a/block")->hasComponent<MeshRendererComponent>(),
          "Scene editor replaced authored mesh with generated shards");
    check(workspace.openPrefabDocument(root / "prefabs/wall.prefab.json", error), error);
    check(workspace.sceneDocument().json() == prefab && workspace.project().world.entities.size() == 2,
          "Prefab editor lost editable source hierarchy");
    check(workspace.editValue({.entityId="block",.component="Fracture3D",.field="pieces"}, 8, false, error), error);
    check(findEntity(workspace.project().world, "block")->component<Transform3DComponent>()->parent == "wall",
          "Editing fracture settings lost the implied transform parent");
    check(workspace.undo(error) && workspace.sceneDocument().json() == prefab, "Fracture Inspector undo lost source");
    check(workspace.editValue({.entityId="block",.component="Fracture3D",.field="density"}, 2400, false, error),
          "Fracture density Inspector edit failed: " + error);
    check(workspace.undo(error) && workspace.sceneDocument().json() == prefab,
          "Density Inspector undo lost source");
    check(workspace.removeComponent("block", "Fracture3D", error), error);
    check(workspace.addComponent("block", "Fracture3D", error), "Could not add default fracture component: " + error);
    check(workspace.editValue({.entityId="block",.component="Fracture3D",.field="anchor_below"}, 0.001, false, error), error);
  }
  const auto cooked = root / "build/cooked";
  const auto diagnostics = assets::cookProject({.projectFile=root / "demi.project.json",.outputDirectory=cooked,.platform="linux"});
  check(!hasErrors(diagnostics), "Component cook failed");
  std::ifstream file(cooked / "prefabs/wall.prefab.json");
  const auto cookedPrefab = J::parse(file);
  check(!assets::hasFractureAuthoring(cookedPrefab), "Cooked prefab executes authoring generator");
  std::ifstream directFile(cooked / "scenes/direct.scene.json");
  check(!assets::hasFractureAuthoring(J::parse(directFile)), "Cooked direct scene retained fracture authoring");
  check(bool(loadProject(cooked / "demi.project.json", error)), "Cooked component load failed: " + error);
  check(composition::expandScene(root / "scenes/direct.scene.json", direct).document.has_value(), "Direct scene components failed");
  auto standalone = source(); standalone["id"] = "scene://standalone";
  standalone["entities"][0]["components"]["Destructible3D"] = J::object();
  standalone["entities"][0]["components"]["Fracture3D"] = {{"pieces", 2}};
  check(composition::expandScene(root / "scenes/standalone.scene.json", standalone).document.has_value(),
        "Both fracture components on a standalone mesh failed");
  auto bad = prefab;
  bad["entities"][0]["components"].erase("Destructible3D");
  check(!composition::expandScene(root / "scenes/bad.scene.json", bad).document, "Orphan Fracture3D accepted");
}
void masonryStreamingAuthoring(const std::filesystem::path &root,
                               bool customModels = false) {
  J prefab = J::parse(R"({"format_version":1,"id":"prefab://wall","entities":[
    {"id":"assembly","components":{"Transform3D":{},"Destructible3D":{}},"children":[
      {"id":"region","components":{"Transform3D":{"position":[0,0.5,0]},
        "Masonry3D":{"size":[1,1,0.1],"rows":2,"columns":2,"anchor_below":0}}}
    ]}]})");
  if (customModels) {
    std::filesystem::create_directories(root);
    writeTetraGlb(root / "brick.glb", true);
    for (const auto *id : {"asset://brick_a", "asset://brick_b"})
      check(!hasErrors(assets::importAsset({.projectDirectory = root,
                                            .source = root / "brick.glb",
                                            .id = id})
                           .diagnostics),
            "Masonry model import failed");
    prefab["entities"][0]["children"][0]["components"]["Masonry3D"]["models"] =
        {{"a", "asset://brick_a"}, {"b", "asset://brick_b"}};
  }
  const std::size_t intactCount = customModels ? 3 : 2;
  write(root / "prefabs/wall.prefab.json", prefab);
  write(root / "demi.project.json",
        {{"format_version", 1},
         {"name", "Masonry test"},
         {"main_scene", "scene://test/main"},
         {"scenes", {{{"id", "scene://test/main"}}}}});
  write(root / "scenes/main.scene.json", {{"format_version", 1},
                                          {"id", "scene://test/main"},
                                          {"entities", J::array()}});
  std::string error;
  auto loaded = loadProject(root / "demi.project.json", error);
  check(bool(loaded), error);
  check(loaded->world.entities.empty(),
        "Inactive template eagerly created entities");
  RuntimePrefabService prefabs;
  prefabs.configure(root);
  WorldCommandBuffer commands;
  auto a = prefabs.instantiate(loaded->world, commands, "prefab://wall",
                               {.id = "a"});
  check(bool(a), "Masonry activation failed");
  (void)commands.flush(loaded->world);
  auto b = prefabs.instantiate(loaded->world, commands, "prefab://wall",
                               {.id = "b", .position = Vec3{3, 0, 0}});
  check(bool(b), "Cached activation failed");
  (void)commands.flush(loaded->world);
  check(a.entityIds.size() == intactCount && b.entityIds.size() == intactCount,
        "Intact masonry eagerly created leaf entities");
  check(prefabs.templateCacheStatistics().hits == 1,
        "Repeated masonry preparation did not reuse its template");
  for (const auto &[part, visual] : findEntity(loaded->world, "b/assembly")
                                        ->component<Destructible3DComponent>()
                                        ->parts)
    check(visual.starts_with("b/") && !findEntity(loaded->world, visual),
          "Cached template leaked another instance identity");
  auto &physics = ensurePhysicsWorld3D(loaded->world);
  physics.step(loaded->world, 1.F / 60);
  check(loaded->world.destruction3D->state("a/assembly").bodies == 1,
        "Masonry did not attach");
  const auto hit=physics.raycast({-.25F,.75F,2},{0,0,-1},4);
  check(hit && hit->entityId=="a/assembly" && !hit->colliderPartId.empty(),"Dormant wall lost part picking");
  const auto mass=findEntity(loaded->world,"a/assembly")->component<Rigidbody3DComponent>()->mass;
  findEntity(loaded->world,"a/assembly")->component<Destructible3DComponent>()->maxBodies=1;
  check(loaded->world.destruction3D->damagePart("a/assembly",hit->colliderPartId,2,error),error);
  physics.step(loaded->world,1.F/60);
  check(loaded->world.destruction3D->state("a/assembly").status=="failed" &&
        findEntity(loaded->world,"a/region"),"Rejected split materialized or removed intact visuals");
  check(loaded->world.entities.size() == 2 * intactCount,
        "Rejected split leaked leaf entities");
  findEntity(loaded->world,"a/assembly")->component<Destructible3DComponent>()->maxBodies=64;
  check(loaded->world.destruction3D->damagePart("a/assembly",hit->colliderPartId,2,error),error);
  physics.step(loaded->world,1.F/60);
  const auto state=loaded->world.destruction3D->state("a/assembly");
  check(state.status=="applied",state.error);
  check(!findEntity(loaded->world,"a/region")->hasComponent<MeshRendererComponent>(),"Split region kept its intact surface");
  if (customModels) {
    check(!findEntity(loaded->world, "a/region/__instances/1"),
          "Split retained a duplicate intact model batch");
    check(findEntity(loaded->world, "b/region/__instances/1"),
          "Split removed an unrelated model batch");
  }
  double splitMass=0;
  for(const auto &entity:loaded->world.entities)
    if(entity.id.starts_with("a/fracture/") || entity.id.starts_with("a/assembly/fracture/"))
      if(const auto *body=entity.component<Rigidbody3DComponent>())splitMass+=body->mass;
  check(std::abs(splitMass-mass)<.001,"Lazy split changed mass");
  const auto &bParts=findEntity(loaded->world,"b/assembly")->component<Destructible3DComponent>()->parts;
  for(const auto &[part,visual]:bParts)check(!findEntity(loaded->world,visual),"Damage activated another wall's leaves");
  const auto checkpoint=loaded->world.destruction3D->checkpoint(loaded->world,"a/assembly",error);
  check(!checkpoint.is_null(),error);
  check(loaded->world.destruction3D->restore("b/assembly",checkpoint,error),error);
  physics.step(loaded->world,1.F/60);
  check(loaded->world.destruction3D->state("b/assembly").status=="applied",loaded->world.destruction3D->state("b/assembly").error);
  check(loaded->world.destruction3D->retireDebris("a/assembly",error),error);
  physics.step(loaded->world,1.F/60);
  const auto retired=loaded->world.destruction3D->checkpoint(loaded->world,"a/assembly",error);
  check(!retired.is_null(),error);
  auto restored=prefabs.instantiate(loaded->world,commands,"prefab://wall",{.id="restored"});
  check(bool(restored),"Could not instantiate checkpoint target");
  (void)commands.flush(loaded->world);
  physics.step(loaded->world,1.F/60);
  check(loaded->world.destruction3D->restore("restored/assembly",retired,error),error);
  physics.step(loaded->world,1.F/60);
  check(loaded->world.destruction3D->state("restored/assembly").status=="applied",loaded->world.destruction3D->state("restored/assembly").error);
  check(prefabs.release(loaded->world, commands, "a"),
        "Release failed with a retired child");
  (void)commands.flush(loaded->world);
  physics.step(loaded->world, 1.F / 60);
  check(!findEntity(loaded->world, "a/assembly") &&
            findEntity(loaded->world, "b/assembly"),
        "Release affected a different instance");
  for(const auto &entity:loaded->world.entities)
    check(!entity.id.starts_with("a/"),"Prefab release leaked lazily created leaves");
  auto multiple=prefab;
  multiple["id"]="prefab://multiple";
  auto nextRegion=multiple["entities"][0]["children"][0];
  nextRegion["id"]="region2";
  nextRegion["components"]["Transform3D"]["position"]={1,.5,0};
  multiple["entities"][0]["children"].push_back(nextRegion);
  write(root/"prefabs/multiple.prefab.json",multiple);
  auto d=prefabs.instantiate(loaded->world,commands,"prefab://multiple",{.id="d",.position=Vec3{10,0,0}});
  check(bool(d) && d.entityIds.size() == 1 + 2 * (intactCount - 1),
        "Multi-region wall was not compact");
  (void)commands.flush(loaded->world);
  physics.step(loaded->world,1.F/60);
  const auto localHit=physics.raycast({9.75F,.75F,2},{0,0,-1},4);
  check(localHit && localHit->entityId=="d/assembly","Multi-region picking failed");
  check(loaded->world.destruction3D->damagePart("d/assembly",localHit->colliderPartId,2,error),error);
  physics.step(loaded->world,1.F/60);
  check(loaded->world.destruction3D->state("d/assembly").status=="applied",loaded->world.destruction3D->state("d/assembly").error);
  check(findEntity(loaded->world,"d/region2")->hasComponent<MeshRendererComponent>() &&
        !findEntity(loaded->world,"d/region")->hasComponent<MeshRendererComponent>(),"Damage expanded an unrelated masonry region");
  const auto &config=*findEntity(loaded->world,"d/assembly")->component<Destructible3DComponent>();
  for(const auto &leaf:(*config.deferredVisuals)["d/region2"])
    check(!findEntity(loaded->world,leaf["id"].get<std::string>()),"Unchanged region has live leaf entities");
  auto changed = prefab;
  changed["entities"][0]["children"][0]["components"]["Masonry3D"]["columns"] =
      3;
  write(root / "prefabs/wall.prefab.json", changed);
  auto c = prefabs.instantiate(loaded->world, commands, "prefab://wall",
                               {.id = "c"});
  check(bool(c) && c.entityIds.size() == intactCount &&
            commands.pendingEntity("c/assembly")
                    ->component<Destructible3DComponent>()
                    ->parts.size() == 6,
        "Template cache ignored source edit");
  if (!customModels) {
    auto reliefSource = prefab;
    reliefSource["entities"][0]["children"][0]["components"]["Masonry3D"]
                ["height_map"] = "asset://height";
    write(root / "prefabs/wall.prefab.json", reliefSource);
    const auto relief =
        composition::bakeFracturePrefab(root / "prefabs/wall.prefab.json");
    check(bool(relief.document), "Runtime relief recipe failed to compile");
    int reliefCells = 0;
    for (const auto &entity : (*relief.document)["entities"])
      if (entity["components"].contains("SurfaceRelief3D")) {
        ++reliefCells;
        check(
            !entity["components"]["MeshRenderer"].contains("model") &&
                !entity["components"]["MeshRenderer"].contains("vertices"),
            "Relief generated a model dependency or serialized mesh geometry");
      }
    check(reliefCells == 1, "Intact region did not share a relief surface");
    const auto preview = composition::expandScene(
        root / "prefabs/wall.prefab.json", reliefSource, false);
    check(bool(preview.document), "Masonry preview expansion failed");
    std::vector<J> previewRelief, runtimeRelief;
    for (const auto &entity : (*preview.document)["entities"]) {
      const auto &components = entity["components"];
      if (!components.contains("SurfaceRelief3D"))
        continue;
      check(!components.contains("Fracture3D"),
            "Preview created fracture simulation inputs");
      previewRelief.push_back(
          {components["MeshRenderer"], components["SurfaceRelief3D"]});
    }
    for (const auto &entity : (*relief.document)["entities"])
      if (entity["components"].contains("Destructible3D"))
        for (const auto &templates :
             entity["components"]["Destructible3D"]["deferred_visuals"])
          for (const auto &leaf : templates)
            runtimeRelief.push_back({leaf["components"]["MeshRenderer"],
                                     leaf["components"]["SurfaceRelief3D"]});
    std::ranges::sort(previewRelief);
    std::ranges::sort(runtimeRelief);
    check(previewRelief == runtimeRelief,
          "Preview atlas/relief descriptors differ from runtime");
  }
  write(root / "prefabs/wall.prefab.json", prefab);
  {
    editor::EditorWorkspace editor;
    check(editor.open(root / "demi.project.json", error), error);
    check(editor.openPrefabDocument(root / "prefabs/wall.prefab.json", error),
          error);
    editor.selectEntity("region/__masonry_preview/cell_0_0");
    check(editor.selectedEntityId()=="region","Generated preview cell did not select its recipe");
    check(editor.editValue(
              {.entityId = "region", .component = "Masonry3D", .field = "rows"},
              3, false, error),
          error);
    check(editor.project().world.entities.size()==8,"Recipe edit did not rebuild preview cells");
    check(editor.undo(error) && editor.sceneDocument().json() == prefab,
          "Masonry Inspector undo changed compact source");
  }
  const auto cooked = root / "build/cooked";
  check(
      !hasErrors(assets::cookProject({.projectFile = root / "demi.project.json",
                                      .outputDirectory = cooked,
                                      .platform = "linux"})),
      "Masonry cook failed");
  std::ifstream file(cooked / "prefabs/wall.prefab.json");
  check(bool(file), "Cook omitted runtime prefab");
  const auto baked=J::parse(file);
  check(!assets::hasMasonryAuthoring(baked),"Cook did not prepare masonry fracture data");
  std::ifstream authored(root / "prefabs/wall.prefab.json");
  check(J::parse(authored)==prefab,"Cook modified the authored wall recipe");
  if (customModels) {
    auto shipping = loadProject(cooked / "demi.project.json", error);
    check(bool(shipping), error);
    RuntimePrefabService cookedPrefabs;
    cookedPrefabs.configure(cooked);
    WorldCommandBuffer pending;
    const auto instance = cookedPrefabs.instantiate(
        shipping->world, pending, "prefab://wall", {.id = "cooked"});
    check(bool(instance) && instance.entityIds.size() == intactCount,
          "Cooked model masonry expanded eagerly");
    (void)pending.flush(shipping->world);
    ensurePhysicsWorld3D(shipping->world).step(shipping->world, 1.F / 60);
    check(shipping->world.destruction3D->state("cooked/assembly").status ==
              "ready",
          shipping->world.destruction3D->state("cooked/assembly").error);
  }
}
void densityAuthoring(const std::filesystem::path &root) {
  auto prefab = J::parse(R"({"format_version":1,"id":"prefab://density","entities":[
    {"id":"wall","components":{"Transform3D":{"scale":[2,1,1]},"Destructible3D":{}},"children":[
      {"id":"light","components":{"Transform3D":{"position":[-0.5,0,0]},
       "MeshRenderer":{"shape":"cube"},"Fracture3D":{"pieces":1,"density":100}}},
      {"id":"heavy","components":{"Transform3D":{"position":[0.5,0,0]},
       "MeshRenderer":{"shape":"cube"},"Fracture3D":{"pieces":1,"density":900}}}
    ]}]})");
  const auto file = root / "prefabs/density.prefab.json";
  write(file, prefab);
  auto baked = composition::bakeFracturePrefab(file);
  check(bool(baked.document), "Density authoring failed");
  auto components = (*baked.document)["entities"][0]["components"];
  check(std::abs(components["Rigidbody3D"]["mass"].get<double>() - 2000) < .01,
        "Density mass must include authored root scale");
  const auto &parts = components["ModelCollider3D"]["inline_geometry"]["parts"];
  check(parts.size() == 2 && parts[0].contains("density") && parts[1].contains("density"),
        "Cooked collider lost per-part density");
  prefab["entities"][0]["components"]["Rigidbody3D"] = {{"mass", 50}};
  write(file, prefab);
  baked = composition::bakeFracturePrefab(file);
  check(baked.document && (*baked.document)["entities"][0]["components"]["Rigidbody3D"]["mass"] == 50,
        "Explicit total mass override ignored");
  for (const J value : {J(0), J(-1), J(1000001), J("steel")}) {
    prefab["entities"][0]["children"][0]["components"]["Fracture3D"]["density"] = value;
    write(file, prefab);
    check(!composition::bakeFracturePrefab(file).document, "Invalid fracture density accepted");
  }
  auto &light = prefab["entities"][0]["children"][0]["components"];
  light["Fracture3D"] = {{"pieces",1},{"collider","box"},{"density",100}};
  light["MeshRenderer"] = {{"shape","mesh"},{"vertices",{{-.5,-.5,0},{.5,-.5,0},{0,.5,.1}}}};
  write(file,prefab);
  baked = composition::bakeFracturePrefab(file);
  check(bool(baked.document), "Explicit proxy rejected detailed non-convex visual");
  bool retained = false;
  for (const auto &entity : (*baked.document)["entities"])
    if (entity["components"].contains("MeshRenderer") &&
        entity["components"]["MeshRenderer"] == light["MeshRenderer"]) {
      retained = true;
      check(entity["components"]["Transform3D"]["position"] == J({-.5,0,0}),
            "Box proxy lost visual placement");
    }
  check(retained, "Box proxy replaced the detailed visual with its collider");
  light["Fracture3D"]["pieces"] = 2;
  write(file,prefab);
  check(!composition::bakeFracturePrefab(file).document,
        "Proxy silently duplicated an unsplittable visual across shards");
}
void test(const std::filesystem::path &root) {
  write(
      root / "demi.project.json",
      {{"format_version", 1},
       {"name", "Fracture component test"},
       {"main_scene", "scene://test/main"},
       {"scenes",
        {{{"id", "scene://test/main"}, {"path", "scenes/main.scene.json"}}}}});
  write(root / "prefabs/broken.prefab.json", fracturePrefab());
  write(
      root / "scenes/main.scene.json",
      J::parse(
          R"({"format_version":1,"id":"scene://test/main","entities":[],"instances":[
    {"id":"a","prefab":"prefab://broken"},
    {"id":"b","prefab":"prefab://broken","overrides":{"body.Transform3D.position":[6,0,0]}}
  ]})"));
  const auto first =
      composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json");
  if (!first.document) {
    std::string error;
    for (const auto &d : first.diagnostics)
      error += d.message + "\n";
    throw std::runtime_error(error);
  }
  const auto second =
      composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json");
  check(first.document == second.document,
        "Repeated fracture generation changed output");
  const auto &geometry =
      (*first.document)["entities"][0]["components"]["ModelCollider3D"]
                       ["inline_geometry"];
  check(geometry["parts"].size() == 12 &&
            !geometry["fracture"]["anchors"].empty(),
        "Generated parts/anchors missing");
  double volume = 0;
  const auto &deferred = (*first.document)["entities"][0]["components"]
                                        ["Destructible3D"]["deferred_visuals"];
  check(deferred.contains("wall") && (*first.document)["entities"].size() == 2,
        "Component fracture did not retain a compact intact source");
  for (const auto &entity : deferred.at("wall")) {
    const auto &mesh = entity["components"]["MeshRenderer"];
    check(mesh["vertices"].size() == mesh["normals"].size() &&
              mesh["vertices"].size() == mesh["uvs"].size(),
          "Generated vertex attributes mismatch");
    const auto &v = mesh["vertices"];
    for (std::size_t i = 0; i < v.size(); i += 3) {
      const auto &a = v[i], &b = v[i + 1], &c = v[i + 2];
      const double ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1],
                   bz = b[2], cx = c[0], cy = c[1], cz = c[2];
      volume += (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) +
                 az * (bx * cy - by * cx)) /
                6;
    }
  }
  check(std::abs(volume - 6.4) < .001,
        "Generated surfaces did not conserve source volume");
  check(!hasErrors(validatePath(root).diagnostics),
        "Generated prefab project validation failed");
  std::string error;
  auto project = loadProject(root / "demi.project.json", error);
  check(bool(project), error);
  auto &world = project->world;
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60);
  auto state = world.destruction3D->state("a/body");
  check(state.status == "ready", state.error);
  check(world.destruction3D->state("b/body").bodies == 1,
        "Second prefab instance failed attachment");
  const auto part = geometry["parts"][0]["id"].get<std::string>();
  check(world.destruction3D->damagePart("a/body", part, 2, error), error);
  physics.step(world, 1.0F / 60);
  check(world.destruction3D->state("a/body").revision == 1,
        "Generated graph did not accept real damage");
  check(world.destruction3D->state("b/body").revision == 0,
        "Prefab instances shared damage");
  const auto cooked = root / "build/cooked";
  auto diagnostics =
      assets::cookProject({.projectFile = root / "demi.project.json",
                           .outputDirectory = cooked,
                           .platform = "linux"});
  if (hasErrors(diagnostics)) {
    std::string issue;
    for (const auto &d : diagnostics)
      issue += d.message + "\n";
    throw std::runtime_error(issue);
  }
  std::ifstream file(cooked / "prefabs/broken.prefab.json");
  J baked;
  file >> baked;
  check(baked["id"] == "prefab://broken" &&
            baked.at("source_recipe") == fracturePrefab(),
        "Cook did not retain component source for runtime overrides");
  check(!hasErrors(validatePath(cooked).diagnostics), "Cooked prefab invalid");
  check(bool(loadProject(cooked / "demi.project.json", error)),
        "Cooked prefab runtime load failed: " + error);
  {
    demi::editor::EditorWorkspace editor;
    check(editor.open(root / "demi.project.json", error),
          "Editor project open failed: " + error);
    const bool opened =
        editor.openPrefabDocument(root / "prefabs/broken.prefab.json", error);
    check(opened, "Editor component preview failed: " + error);
    check(editor.project().world.entities.size() ==
              (*first.document)["entities"].size(),
          "Editor preview lost authored entities");
    check(editor.sceneDocument().json() == fracturePrefab() &&
              !editor.sceneDocument().isDirty(),
          "Preview materialized generated geometry into authored components");
  }
  auto bad = fracturePrefab();
  bad["instances"] = {{{"id", "cycle"}, {"prefab", "prefab://broken"}}};
  write(root / "prefabs/broken.prefab.json", bad);
  check(!composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
             .document,
        "Component prefab dependency cycle accepted");
  bad = fracturePrefab();
  bad["entities"][0]["children"][0]["components"]["Fracture3D"]["pieces"] =
      std::int64_t(INT32_MAX) + 1;
  write(root / "prefabs/broken.prefab.json", bad);
  check(!composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
             .document,
        "Unrepresentable piece count accepted");
  auto changed = fracturePrefab();
  changed["entities"][0]["children"][0]["components"]["MeshRenderer"]["size"] =
      {5, 4, .4};
  write(root / "prefabs/broken.prefab.json", changed);
  check(composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
                .document != first.document,
        "Source edit did not regenerate output");
  importedModel(root);
}
} // namespace
int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-fracture-prefab-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(root))
    return 1;
  try {
    test(root);
    componentAuthoring(root / "components");
    lazyImportedSources(root / "lazy-models");
    densityAuthoring(root / "components");
    masonryStreamingAuthoring(root / "masonry");
    masonryStreamingAuthoring(root / "masonry-models", true);
    obsoleteRecipeRejected(root / "removed-format");
  } catch (const std::exception &e) {
    std::cerr << e.what() << " fixtures=" << root << '\n';
    return 1;
  }
  std::filesystem::remove_all(root);
  std::cout << "Fracture prefab generation, runtime and cook passed\n";
}
