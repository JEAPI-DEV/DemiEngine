#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "editor/EditorSourceCreation.h"
#include "editor/EditorWorkspace.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace demi;
using namespace demi::editor;
void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
void write(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << value.dump(2) << '\n';
  require(out.good(), "Could not write fixture");
}
struct Fixture {
  fs::path root;
  Fixture() {
    auto pattern =
        (fs::temp_directory_path() / "demi-prefab-conversion-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const auto *created = mkdtemp(buffer.data());
    require(created, "Could not create fixture directory");
    root = created;
    write(root / "demi.project.json", Json::parse(R"({
      "format_version":1,"name":"Conversion","main_scene":"scene://main",
      "scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
    })"));
    write(root / "prefabs/sensor.prefab.json", Json::parse(R"({
      "format_version":1,"id":"prefab://sensor","entities":[
        {"id":"body","components":{"Transform3D":{"position":[0,2,0]}}}]
    })"));
    write(root / "scenes/main.scene.json", Json::parse(R"({
      "format_version":1,"id":"scene://main","entities":[
        {"id":"platform","components":{"Transform3D":{"position":[10,4,8],"scale":[2,2,2]}},"children":[
          {"id":"habitat","name":"Habitat","components":{"Transform3D":{"position":[3,1,2]},"MeshRenderer":{"shape":"cube"}},"children":[
            {"id":"door","components":{"Transform3D":{"position":[1,0,0]}}},
            {"id":"sensor","prefab":"prefab://sensor"}
          ]}
        ]},
        {"id":"observer","components":{"Transform3D":{},"GameplayData":{"values":{"target":"habitat","door":"door"}}}}
      ]
    })"));
  }
  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};
void conversion() {
  Fixture fixture;
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(fixture.root, error), error);
  workspace.selectEntity("habitat");
  const auto before = workspace.sceneDocument().json();
  std::map<std::string, runtime::Vec3> positions;
  for (const auto &entity : workspace.project().world.entities)
    if (const auto transform =
            runtime::resolveWorldTransform3D(workspace.project().world, entity))
      positions[entity.id] = transform->position;
  fs::path prefab;
  require(createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                             "habitat", prefab, error, "habitat"),
          error);
  const auto converted = workspace.sceneDocument().json();
  require(
      converted != before && workspace.selectedEntityId() == "habitat",
      "Default conversion did not replace the hierarchy or retain selection");
  for (const auto &[id, position] : positions) {
    const auto *entity = runtime::findEntity(workspace.project().world, id);
    require(entity, "Conversion changed a stable ID: " + id);
    const auto transform =
        runtime::resolveWorldTransform3D(workspace.project().world, *entity);
    require(transform && transform->position.x == position.x &&
                transform->position.y == position.y &&
                transform->position.z == position.z,
            "Conversion changed a world transform: " + id);
  }
  for (const char *id : {"habitat", "door", "sensor/body"}) {
    const auto *entity = runtime::findEntity(workspace.project().world, id);
    require(entity && entity->prefabInstance == "habitat_instance" &&
                entity->prefabLocalId == id,
            "Mapped entity lost its Inspector override origin");
  }
  require(converted["entities"][1] == before["entities"][1],
          "Conversion rewrote external gameplay references");
  require(workspace.undo(error), error);
  require(
      workspace.sceneDocument().json() == before && fs::exists(prefab),
      "Undo failed to restore source nesting or removed the reusable prefab");
  require(workspace.redo(error), error);
  require(workspace.sceneDocument().json() == converted,
          "Redo changed the conversion");
  require(
      workspace.editValue(
          {.entityId = "door", .component = "Transform3D", .field = "position"},
          {4, 0, 0}, false, error),
      error);
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == converted,
          "Mapped override Undo did not restore source");
  require(workspace.duplicatePrefabInstance("habitat", error), error);
  const auto duplicate = std::string(workspace.selectedEntityId());
  require(!duplicate.empty() && duplicate != "habitat" &&
              runtime::findEntity(workspace.project().world, "habitat"),
          "Duplicate reused an original entity ID");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == converted,
          "Duplicate Undo changed the original instance");
  require(workspace.saveAll(error), error);
  std::ifstream input(prefab);
  auto source = Json::parse(input);
  source["entities"][0]["name"] = "Updated habitat";
  write(prefab, source);
  require(workspace.refresh(error), error);
  require(runtime::findEntity(workspace.project().world, "habitat")->name ==
              "Updated habitat",
          "Prefab source edits did not reach the converted instance");
  const auto expanded = runtime::composition::expandScene(
      fixture.root / "scenes/main.scene.json", converted);
  require(expanded.document.has_value(),
          "Mapped scene failed runtime expansion");
  const runtime::composition::PrefabOriginIndex origins(*expanded.document);
  require(origins.find("habitat")->localEntityId == "habitat",
          "Cooked origin metadata lost the preserved ID");
  write(fixture.root / "scenes/main.scene.json", *expanded.document);
  EditorWorkspace cooked;
  require(cooked.open(fixture.root, error), error);
  require(
      runtime::findEntity(cooked.project().world, "habitat")->prefabInstance ==
          "habitat_instance",
      "Expanded scene did not reload its mapped instance ownership");
}
void conversion2D() {
  Fixture fixture;
  write(fixture.root / "scenes/main.scene.json", Json::parse(R"({
    "format_version":1,"id":"scene://main","entities":[
      {"id":"parent","components":{"Transform2D":{"position":[12,8]}},"children":[
        {"id":"body","components":{"Transform2D":{"position":[3,4]}},"children":[
          {"id":"label","components":{"Transform2D":{"position":[1,2]}}}]}]}]
  })"));
  EditorWorkspace workspace;
  std::string error;
  fs::path prefab;
  require(workspace.open(fixture.root, error), error);
  workspace.selectEntity("body");
  const auto before = workspace.sceneDocument().json();
  require(createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                             "body", prefab, error, "body"),
          error);
  const auto *body = runtime::findEntity(workspace.project().world, "body");
  const auto *label = runtime::findEntity(workspace.project().world, "label");
  require(body && label &&
              body->component<runtime::Transform2DComponent>()->parent ==
                  "parent" &&
              label->component<runtime::Transform2DComponent>()->parent ==
                  "body" &&
              body->component<runtime::Transform2DComponent>()->position.x == 3,
          "2D conversion changed IDs, parentage or local pose");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == before,
          "2D Undo did not restore nesting");
}

void copyAndFailure() {
  Fixture fixture;
  EditorWorkspace workspace;
  std::string error;
  fs::path created;
  require(workspace.open(fixture.root, error), error);
  workspace.selectEntity("habitat");
  const auto before = workspace.sceneDocument().json();
  require(createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                             "copy", created, error, "habitat", {},
                             {.replaceSelectionWithPrefab = false}),
          error);
  require(workspace.sceneDocument().json() == before,
          "Copy-only changed the scene");
  require(!createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                              "copy", created, error, "habitat"),
          "Overwrote a prefab");
  fs::remove(fixture.root / "prefabs/sensor.prefab.json");
  require(!createEditorSource(workspace, EditorSourceKind::PrefabFromSelection,
                              "failed", created, error, "habitat"),
          "Invalid prefab was linked");
  require(workspace.sceneDocument().json() == before &&
              !fs::exists(fixture.root / "prefabs/failed.prefab.json"),
          "Failed conversion left a changed scene or partial source");
}
} // namespace
int main() {
  try {
    conversion();
    conversion2D();
    copyAndFailure();
    std::cout << "Prefab conversion checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
