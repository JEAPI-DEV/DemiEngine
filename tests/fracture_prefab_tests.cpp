#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/ConvexFracture.h"
#include "demi/assets/FracturePrefab.h"
#include "demi/assets/FractureAuthoring.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/schema/Validation.h"
#include "editor/EditorWorkspace.h"
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
J recipe() {
  return J::parse(R"({"format_version":1,"id":"prefab://broken","fracture":{
  "generator_version":1,"source":"prefab://source","seed":123,
  "objects":{"wall":{"pieces":12,"anchor_below":0.001}}
}})");
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
  auto model = source();
  auto &renderer = model["entities"][0]["components"]["MeshRenderer"];
  renderer["model"] = "asset://tetra";
  renderer["size"] = {1, 1, 1};
  write(root / "prefabs/source.prefab.json", model);
  auto config = recipe();
  config["fracture"]["objects"]["wall"]["pieces"] = 4;
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

void componentAuthoring(const std::filesystem::path &root) {
  const auto prefab = J::parse(R"({"format_version":1,"id":"prefab://wall","entities":[
    {"id":"wall","components":{"Transform3D":{"position":[3,0,0]},"Destructible3D":{},
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
  check((*baked.document)["entities"][1]["id"] == "block" &&
        (*baked.document)["entities"][1]["components"].contains("GameplayData") &&
        !(*baked.document)["entities"][1]["components"].contains("MeshRenderer"),
        "Source identity/behavior lost or source mesh rendered twice");
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
void test(const std::filesystem::path &root) {
  write(
      root / "demi.project.json",
      {{"format_version", 1},
       {"name", "Fracture recipe test"},
       {"main_scene", "scene://test/main"},
       {"scenes",
        {{{"id", "scene://test/main"}, {"path", "scenes/main.scene.json"}}}}});
  write(root / "prefabs/source.prefab.json", source());
  write(root / "prefabs/broken.prefab.json", recipe());
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
  for (const auto &entity : (*first.document)["entities"]) {
    if (!entity["components"].contains("MeshRenderer"))
      continue;
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
  check(!baked.contains("fracture") && baked["id"] == "prefab://broken",
        "Cook did not bake recipe to stable prefab data");
  check(!hasErrors(validatePath(cooked).diagnostics), "Cooked prefab invalid");
  check(bool(loadProject(cooked / "demi.project.json", error)),
        "Cooked prefab runtime load failed: " + error);
  {
    demi::editor::EditorWorkspace editor;
    check(editor.open(root / "demi.project.json", error),
          "Editor project open failed: " + error);
    const bool opened =
        editor.openPrefabDocument(root / "prefabs/broken.prefab.json", error);
    check(opened, "Editor recipe preview failed: " + error);
    check(editor.project().world.entities.size() ==
              (*first.document)["entities"].size(),
          "Editor preview differs from compiled prefab");
    check(editor.sceneDocument().json().contains("fracture") &&
              !editor.sceneDocument().isDirty(),
          "Preview materialized generated geometry into authored recipe");
  }
  auto bad = recipe();
  bad["fracture"]["source"] = "prefab://broken";
  write(root / "prefabs/broken.prefab.json", bad);
  check(!composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
             .document,
        "Recipe dependency cycle accepted");
  bad = recipe();
  bad["fracture"]["objects"]["wall"]["pieces"] = 10000;
  write(root / "prefabs/broken.prefab.json", bad);
  check(!composition::bakeFracturePrefab(root / "prefabs/broken.prefab.json")
             .document,
        "Unbounded recipe accepted");
  write(root / "prefabs/broken.prefab.json", recipe());
  auto changed = source();
  changed["entities"][0]["components"]["MeshRenderer"]["size"] = {5, 4, .4};
  write(root / "prefabs/source.prefab.json", changed);
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
  } catch (const std::exception &e) {
    std::cerr << e.what() << " fixtures=" << root << '\n';
    return 1;
  }
  std::filesystem::remove_all(root);
  std::cout << "Fracture prefab generation, runtime and cook passed\n";
}
