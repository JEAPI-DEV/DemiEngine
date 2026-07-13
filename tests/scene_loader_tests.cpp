#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/hud/HudElementRegistry.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

namespace {

using namespace demi::runtime;

bool writeFile(const std::filesystem::path &path, const char *contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << contents;
  return true;
}

} // namespace

int main(int argc, char **argv) {
  namespace runtime = demi::runtime;

  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1])
                                              : std::filesystem::current_path();

  const runtime::scene_loading::ComponentDescriptor *transform2D =
      runtime::scene_loading::findComponentDescriptor("Transform2D");
  const runtime::scene_loading::ComponentDescriptor *animation =
      runtime::scene_loading::findComponentDescriptor("AnimationPlayer3D");
  if (transform2D == nullptr || transform2D->parse == nullptr ||
      transform2D->serialize == nullptr || transform2D->fields.empty() ||
      transform2D->defaults == nullptr ||
      transform2D->editor.category != "2D" || !transform2D->exposedToLua ||
      animation == nullptr || animation->parse == nullptr ||
      animation->name != "AnimationPlayer3D" ||
      runtime::scene_loading::findComponentDescriptor("NotAComponent") !=
          nullptr) {
    std::cerr
        << "Component registry does not provide canonical component lookup.\n";
    return 1;
  }

  runtime::Entity metadataEntity;
  const nlohmann::json transformJson = {{"position", {2.0, 3.0}},
                                        {"rotation", 0.5}};
  transform2D->parse(transformJson, metadataEntity);
  if (transform2D->serialize(metadataEntity) != transformJson ||
      !runtime::scene_loading::componentDefaults(*transform2D).empty() ||
      runtime::scene_loading::validateComponent(*transform2D, transformJson)
              .size() != 0 ||
      runtime::scene_loading::validateComponent(
          *transform2D, nlohmann::json{{"position", "wrong"}})
          .empty() ||
      !runtime::scene_loading::canonicalComponentSchema()["properties"]
           .contains("GameplayData")) {
    std::cerr << "Component metadata defaults, validation, schema, or "
                 "round-trip failed.\n";
    return 1;
  }

  const std::filesystem::path invalidComponentFixture =
      std::filesystem::temp_directory_path() /
      "demi_unknown_component.scene.json";
  if (!writeFile(invalidComponentFixture, R"json({
    "format_version": 1,
    "id": "scene://fixture/unknown",
    "entities": [{"id": "ent_unknown", "name": "Unknown", "components": {
      "UnregisteredHealth": {"current": 10}
    }}]
  })json")) {
    std::cerr << "Failed to write unknown-component fixture.\n";
    return 1;
  }
  const demi::Diagnostics unknownDiagnostics = demi::validateTextFile(
      invalidComponentFixture, demi::SourceFileKind::Scene);
  if (std::ranges::none_of(unknownDiagnostics, [](const auto &diagnostic) {
        return diagnostic.code == "SCENE_UNKNOWN_COMPONENT";
      })) {
    std::cerr << "Unknown scene components were not rejected.\n";
    return 1;
  }

  const runtime::scene_loading::HudElementDescriptor *buttonDescriptor =
      runtime::scene_loading::findHudElementDescriptor("button");
  if (buttonDescriptor == nullptr || buttonDescriptor->parse == nullptr ||
      runtime::scene_loading::findHudElementDescriptor("not-an-element") !=
          nullptr) {
    std::cerr << "HUD registry does not provide canonical element lookup.\n";
    return 1;
  }

  std::string error;
  const std::optional<runtime::LoadedProject> loaded = runtime::loadProject(
      root / "examples" / "minimal_2d_networking" / "demi.project.json", error);
  if (!loaded.has_value()) {
    std::cerr << "Failed to load minimal_2d_networking project: " << error
              << '\n';
    return 1;
  }

  const runtime::Entity *camera =
      runtime::findEntity(loaded->world, "ent_camera_menu");
  if (camera == nullptr || !camera->hasComponent<Transform2DComponent>() ||
      !camera->hasComponent<Camera2DComponent>()) {
    std::cerr << "Scene loader did not read nested camera components.\n";
    return 1;
  }

  const runtime::Entity *controller =
      runtime::findEntity(loaded->world, "ent_menu_controller");
  if (controller == nullptr ||
      !controller->hasComponent<LuaScriptComponent>() ||
      controller->component<LuaScriptComponent>()->module !=
          "script://scripts/menu_scene.lua") {
    std::cerr << "Scene loader did not read nested LuaScript component.\n";
    return 1;
  }

  bool foundNetworkButton = false;
  for (const runtime::HudButtonElement &button : loaded->world.hudButtons) {
    if (button.id == "menu_button_network") {
      foundNetworkButton = button.action == "menu_button_network";
      break;
    }
  }
  if (!foundNetworkButton) {
    std::cerr << "HUD loader did not preserve menu button action.\n";
    return 1;
  }

  const std::filesystem::path hudFixture =
      std::filesystem::temp_directory_path() /
      "demi_scene_loader_hud_fixture.json";
  if (!writeFile(hudFixture, R"json({
    "canvas_size": [960.0, 540.0],
    "elements": [
      {
        "type": "text",
        "id": "hud_text_font_size",
        "text": "Readable",
        "position": [12.0, 16.0],
        "font_size": 32.0
      },
      {
        "type": "button",
        "id": "hud_button_font_size",
        "label": "OK",
        "position": [12.0, 64.0],
        "size": [120.0, 40.0],
        "font_size": 24.0
      },
      {
        "type": "panel",
        "id": "hud_panel",
        "position": [100.0, 120.0],
        "size": [220.0, 80.0],
        "corner_radius": 12.0,
        "border_width": 2.0,
        "color": [0.06, 0.07, 0.18, 0.70],
        "border_color": [1.0, 1.0, 1.0, 0.40]
      },
      {
        "type": "circle",
        "id": "hud_circle",
        "center": [64.0, 220.0],
        "radius": 28.0,
        "color": [0.06, 0.07, 0.18, 0.50]
      }
    ]
  })json")) {
    std::cerr << "Failed to write HUD parser fixture.\n";
    return 1;
  }
  runtime::World hudWorld;
  runtime::scene_loading::loadHudFile(hudWorld, hudFixture, error);
  if (hudWorld.hudText.empty() || hudWorld.hudText[0].fontSize != 32.0F ||
      hudWorld.hudButtons.empty() || hudWorld.hudButtons[0].fontSize != 24.0F ||
      hudWorld.hudPanels.empty() ||
      hudWorld.hudPanels[0].cornerRadius != 12.0F ||
      hudWorld.hudPanels[0].borderWidth != 2.0F ||
      hudWorld.hudCircles.empty() || hudWorld.hudCircles[0].radius != 28.0F) {
    std::cerr
        << "HUD loader did not read panel, circle, or font_size fields.\n";
    return 1;
  }
  if (!hudWorld.ui.nodes.empty()) {
    std::cerr << "Flat HUD was also loaded into the tree UI representation.\n";
    return 1;
  }

  const std::filesystem::path meshFixture =
      std::filesystem::temp_directory_path() / "demi_scene_loader_mesh_fixture";
  std::error_code fsError;
  std::filesystem::remove_all(meshFixture, fsError);
  if (!writeFile(meshFixture / "demi.project.json", R"json({
    "format_version": 1,
    "name": "Mesh Fixture",
    "main_scene": "scene://fixture/main",
    "scenes": [
      {
        "id": "scene://fixture/main",
        "path": "scenes/main.scene.json"
      }
    ]
  })json") ||
      !writeFile(meshFixture / "scenes" / "main.scene.json", R"json({
    "format_version": 1,
    "id": "scene://fixture/main",
    "name": "Mesh Fixture Scene",
    "entities": [
      {
        "id": "ent_mesh_0",
        "name": "Generated Mesh",
        "components": {
          "Transform3D": {
            "position": [0.0, 0.0, 0.0]
          },
          "MeshRenderer": {
            "vertices": [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
            "normals": [[0.0, 0.0, 1.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0]],
            "uvs": [[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]]
          }
        }
      }
    ]
  })json")) {
    std::cerr << "Failed to write mesh scene loader fixture.\n";
    return 1;
  }

  const std::optional<runtime::LoadedProject> meshProject =
      runtime::loadProject(meshFixture / "demi.project.json", error);
  if (!meshProject.has_value()) {
    std::cerr << "Failed to load mesh fixture project: " << error << '\n';
    return 1;
  }

  const runtime::Entity *mesh =
      runtime::findEntity(meshProject->world, "ent_mesh_0");
  if (mesh == nullptr || !mesh->hasComponent<MeshRendererComponent>() ||
      mesh->component<MeshRendererComponent>()->vertices.size() != 3 ||
      mesh->component<MeshRendererComponent>()->normals.size() != 3 ||
      mesh->component<MeshRendererComponent>()->uvs.size() != 3 ||
      mesh->component<MeshRendererComponent>()->vertices[1].x != 1.0F ||
      mesh->component<MeshRendererComponent>()->uvs[2].y != 1.0F) {
    std::cerr << "Scene loader did not read dynamic MeshRenderer data.\n";
    return 1;
  }

  const std::filesystem::path gameplayFixture =
      std::filesystem::temp_directory_path() /
      "demi_scene_loader_gameplay_fixture";
  std::filesystem::remove_all(gameplayFixture, fsError);
  if (!writeFile(gameplayFixture / "demi.project.json", R"json({
    "format_version": 1, "name": "Gameplay Fixture", "main_scene": "scene://fixture/main",
    "performance_budgets": {"maximum_frame_ms": 12.5, "maximum_draw_calls": 42,
      "maximum_resident_assets": 24},
    "scenes": [{"id": "scene://fixture/main", "path": "scenes/main.scene.json"}]
  })json") ||
      !writeFile(gameplayFixture / "scenes" / "main.scene.json", R"json({
    "format_version": 1, "id": "scene://fixture/main", "entities": [{
      "id": "ent_gameplay", "name": "Gameplay Entity", "components": {
        "GameplayData": {"values": {"health": {"current": 50, "maximum": 100}}},
        "Transform2D": {"position": [2.0, 3.0]}
      }
    }]
  })json")) {
    std::cerr << "Failed to write gameplay component fixture.\n";
    return 1;
  }

  const std::optional<runtime::LoadedProject> gameplayProject =
      runtime::loadProject(gameplayFixture / "demi.project.json", error);
  const runtime::Entity *gameplay =
      gameplayProject.has_value()
          ? runtime::findEntity(gameplayProject->world, "ent_gameplay")
          : nullptr;
  const std::string *gameplayData =
      gameplay != nullptr
          ? runtime::serializedComponent(*gameplay, "GameplayData")
          : nullptr;
  if (!gameplayProject ||
      gameplayProject->project.performanceBudgets.maximumFrameMilliseconds !=
          12.5F ||
      gameplayProject->project.performanceBudgets.maximumDrawCalls != 42 ||
      gameplayProject->project.performanceBudgets.maximumResidentAssets != 24 ||
      gameplayData == nullptr ||
      gameplayData->find("\"current\":50") == std::string::npos ||
      !gameplay->hasComponent<GameplayDataComponent>() ||
      !gameplay->hasComponent<Transform2DComponent>()) {
    std::cerr
        << "Scene loader did not preserve generic gameplay component data.\n";
    return 1;
  }

  return 0;
}
