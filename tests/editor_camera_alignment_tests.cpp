#include "demi/runtime/scene/WorldQueries.h"
#include "editor/EditorWorkspace.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace demi;
using namespace demi::editor;
using Json = nlohmann::json;
namespace fs = std::filesystem;
void require(bool value, const std::string &error) {
  if (!value)
    throw std::runtime_error(error);
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
        (fs::temp_directory_path() / "demi-camera-align-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    const auto *path = mkdtemp(bytes.data());
    require(path, "No fixture directory");
    root = path;
    write(root / "demi.project.json",
          Json::parse(R"({"format_version":1,"name":"Camera alignment",
      "main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})"));
  }
  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};
bool near(float a, float b) { return std::abs(a - b) < 0.002F; }
void same(runtime::Vec3 a, runtime::Vec3 b) {
  require(near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z),
          "Camera vectors differ");
}
runtime::Vec3 unit(runtime::Vec3 value) {
  const auto length =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return {value.x / length, value.y / length, value.z / length};
}
void align3D(bool prefab, bool orthographic) {
  Fixture fixture;
  Json camera = Json::parse(R"({"id":"camera","components":{
    "Transform3D":{"position":[2,4,3],"rotation":[0.3,0.2,0.5],"scale":[2,1,3]},
    "Camera3D":{"fov":41,"clear_color":[0.2,0.3,0.4,1],"priority":7,"viewport_x":0.1,
      "viewport_width":0.8,"render_hud":true,"up_axis":-1,"target_offset":[1,0,1]}}})");
  if (prefab)
    write(fixture.root / "prefabs/camera.prefab.json",
          {{"format_version", 1},
           {"id", "prefab://camera"},
           {"entities", Json::array({camera})}});
  const Json child =
      prefab ? Json{{"id", "rig"},
                    {"prefab", "prefab://camera"},
                    {"entity_ids", {{"camera", "camera"}}},
                    {"overrides", {{"camera.Camera3D.priority", 9}}}}
             : camera;
  Json scene = {
      {"format_version", 1},
      {"id", "scene://main"},
      {"entities", Json::array({{{"id", "parent"},
                                 {"components",
                                  {{"Transform3D",
                                    {{"position", {20, 8, -10}},
                                     {"rotation", {0.2, 0.6, 0.4}},
                                     {"scale", {2, 3, -1}}}}}},
                                 {"children", Json::array({child})}}})}};
  write(fixture.root / "scenes/main.scene.json", scene);
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(fixture.root, error), error);
  workspace.selectEntity("camera");
  workspace.sceneView().update({.mouseDelta = {34, -17},
                                .viewportSize = {800, 600},
                                .wheel = -2,
                                .hovered = true,
                                .focused = true,
                                .orbitButton = true,
                                .orbitModifier = true});
  workspace.sceneView().setProjection(orthographic
                                          ? EditorProjection::Orthographic
                                          : EditorProjection::Perspective);
  const auto view = workspace.sceneView().camera();
  const auto before = workspace.sceneDocument().json();
  const auto initial = *runtime::findEntity(workspace.project().world, "camera")
                            ->component<runtime::Camera3DComponent>();
  require(workspace.canAlignSelectedCameraToView(),
          "Camera action unavailable");
  require(workspace.alignSelectedCameraToView(error), error);
  const auto &world = workspace.project().world;
  const auto *entity = runtime::findEntity(world, "camera");
  const auto transform = runtime::resolveWorldTransform3D(world, *entity);
  require(transform.has_value(), "Lost camera transform");
  const auto &result = *entity->component<runtime::Camera3DComponent>();
  same(transform->position, view.position);
  same(unit(runtime::transformDirection3D(*transform, result.targetOffset)),
       view.forward);
  same(unit(runtime::transformDirection3D(*transform, {0, result.upAxis, 0})),
       view.up);
  same(entity->component<runtime::Transform3DComponent>()->scale, {2, 1, 3});
  require(result.perspective == view.projection.perspective &&
              near(orthographic ? result.orthographicSize : result.fov,
                   orthographic ? view.projection.orthographicSize
                                : view.projection.fov),
          "Projection did not match");
  require(result.priority == initial.priority &&
              result.viewportX == initial.viewportX &&
              result.viewportWidth == initial.viewportWidth &&
              result.renderHud == initial.renderHud &&
              result.clearColor.r == initial.clearColor.r,
          "Alignment changed unrelated camera settings");
  same(workspace.sceneView().camera().position, view.position);
  const auto after = workspace.sceneDocument().json();
  require(after != before, "Alignment made no authoring change");
  require(workspace.alignSelectedCameraToView(error), error);
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == before &&
              !workspace.sceneDocument().canUndo(),
          "One Undo did not restore all camera fields and override structure");
  require(workspace.redo(error), error);
  require(workspace.sceneDocument().json() == after,
          "Redo changed alignment fields");
  require(workspace.saveAll(error), error);
  EditorWorkspace reopened;
  require(reopened.open(fixture.root, error), error);
  same(runtime::worldPosition3D(
           reopened.project().world,
           *runtime::findEntity(reopened.project().world, "camera")),
       view.position);
  workspace.selectEntity("parent");
  require(!workspace.alignSelectedCameraToView(error),
          "Non-camera selection accepted");
  workspace.selectEntity("camera");
  workspace.toggleEntitySelection("parent");
  require(!workspace.alignSelectedCameraToView(error),
          "Ambiguous selection accepted");
  workspace.selectEntity("parent");
  require(
      workspace.editValue(
          {.entityId = "parent", .component = "Transform3D", .field = "scale"},
          {0, 1, 1}, false, error),
      error);
  workspace.selectEntity("camera");
  const auto singular = workspace.sceneDocument().json();
  require(!workspace.alignSelectedCameraToView(error) &&
              workspace.sceneDocument().json() == singular,
          "Singular parent partially changed the camera");
}
void fieldTransactionRejectsPartialEdits() {
  Fixture fixture;
  const auto path = fixture.root / "scenes/main.scene.json";
  const Json source =
      Json::parse(R"({"format_version":1,"id":"scene://main","entities":[
    {"id":"camera","components":{"Transform3D":{},"Camera3D":{}}}]})");
  write(path, source);
  EditorSceneDocument document;
  std::string error;
  require(document.open(path, error), error);
  require(
      !document.setFieldValues(
          {{{.entityId = "camera",
             .component = "Transform3D",
             .field = "position"},
            {1, 2, 3}},
           {{.entityId = "missing", .component = "Camera3D", .field = "fov"},
            50}},
          error),
      "Invalid transaction was accepted");
  require(document.json() == source && !document.canUndo(),
          "Failed transaction applied some fields");
}

void align2D() {
  Fixture fixture;
  write(fixture.root / "scenes/main.scene.json",
        Json::parse(R"({"format_version":1,"id":"scene://main","entities":[
    {"id":"parent","components":{"Transform2D":{"position":[10,8],"rotation":0.5,"scale":[2,3]}},"children":[
      {"id":"camera","components":{"Transform2D":{"position":[1,2]},"Camera2D":{"orthographic_size":12,"follow_speed":3}}}]}]})"));
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(fixture.root, error), error);
  workspace.selectEntity("camera");
  workspace.sceneView2D().update({.mouseDelta = {80, 30},
                                  .viewportSize = {800, 600},
                                  .wheel = 2,
                                  .hovered = true,
                                  .focused = true,
                                  .panButton = true});
  const auto view = workspace.sceneView2D().camera();
  const auto before = workspace.sceneDocument().json();
  require(workspace.alignSelectedCameraToView(error), error);
  const auto *camera = runtime::findEntity(workspace.project().world, "camera");
  const auto position =
      runtime::worldPosition2D(workspace.project().world, *camera);
  require(
      near(position.x, view.position.x) && near(position.y, view.position.y) &&
          near(
              camera->component<runtime::Camera2DComponent>()->orthographicSize,
              view.projection.orthographicSize),
      "2D framing mismatch");
  require(camera->component<runtime::Camera2DComponent>()->followSpeed == 3,
          "Lost camera follow settings");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == before, "2D Undo mismatch");
}
} // namespace
int main() {
  try {
    align3D(false, false);
    align3D(false, true);
    align3D(true, false);
    align3D(true, true);
    align2D();
    fieldTransactionRejectsPartialEdits();
    std::cout << "Camera alignment checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
