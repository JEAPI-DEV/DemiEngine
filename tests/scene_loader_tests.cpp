#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/ui/UiModel.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <limits>
#include <fstream>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

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
  const nlohmann::json transformDefaults = {
      {"parent", ""},
      {"position", {0.0, 0.0}},
      {"rotation", 0.0},
      {"scale", {1.0, 1.0}}};
  transform2D->parse(transformJson, metadataEntity);
  if (transform2D->serialize(metadataEntity) != transformJson ||
      runtime::scene_loading::componentDefaults(*transform2D) !=
          transformDefaults ||
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

  const auto *meshDescriptor =
      runtime::scene_loading::findComponentDescriptor("MeshRenderer");
  if (meshDescriptor == nullptr)
    return 1;
  const auto &meshDefaults =
      runtime::scene_loading::componentDefaults(*meshDescriptor);
  const auto meshSchema = runtime::scene_loading::componentSchema(*meshDescriptor);
  for (const char *field : {"metallic", "roughness", "opacity", "surface_mode"}) {
    if (meshDefaults.contains(field) ||
        meshSchema["properties"][field].contains("default")) {
      std::cerr << "Mesh surface override invented a canonical default.\n";
      return 1;
    }
  }
  runtime::Entity meshEntity;
  const nlohmann::json surface = {{"metallic", 0.5},
                                  {"roughness", 0.75},
                                  {"opacity", 0.25},
                                  {"surface_mode", "transparent"}};
  meshDescriptor->parse(surface, meshEntity);
  const auto *surfaceMesh = meshEntity.component<runtime::MeshRendererComponent>();
  nlohmann::json encoded;
  if (surfaceMesh == nullptr || surfaceMesh->metallic != 0.5F ||
      surfaceMesh->roughness != 0.75F || surfaceMesh->opacity != 0.25F ||
      surfaceMesh->surfaceMode != "transparent" ||
      !runtime::MeshRendererComponent::serializeField(*surfaceMesh, "metallic", encoded) ||
      encoded != surface["metallic"] ||
      !runtime::MeshRendererComponent::serializeField(*surfaceMesh, "surface_mode", encoded) ||
      encoded != "transparent" ||
      meshDescriptor->serialize(meshEntity) != surface) {
    std::cerr << "Mesh surface overrides did not round-trip.\n";
    return 1;
  }
  auto patchedSurface = surface;
  patchedSurface["roughness"] = 0.5;
  if (!meshDescriptor->patch(meshEntity, "roughness", patchedSurface) ||
      meshEntity.component<runtime::MeshRendererComponent>()->roughness != 0.5F ||
      meshEntity.component<runtime::MeshRendererComponent>()->metallic != 0.5F ||
      meshEntity.component<runtime::MeshRendererComponent>()->surfaceMode !=
          "transparent") {
    std::cerr << "Mesh surface field mutation changed unrelated state.\n";
    return 1;
  }
  for (const nlohmann::json invalid : {
           nlohmann::json{{"metallic", -0.1}},
           nlohmann::json{{"roughness", 1.1}},
           nlohmann::json{{"opacity", nullptr}},
           nlohmann::json{{"opacity", "half"}},
           nlohmann::json{{"surface_mode", "alpha"}},
           nlohmann::json{{"surface_mode", nullptr}},
           nlohmann::json{{"metallic", std::numeric_limits<double>::infinity()}}}) {
    bool rejected = false;
    try {
      runtime::MeshRendererComponent::parse(invalid, meshEntity);
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected || runtime::scene_loading::validateComponent(*meshDescriptor, invalid).empty()) {
      std::cerr << "Invalid mesh surface override was accepted.\n";
      return 1;
    }
  }

  const nlohmann::json inlineColors = {
      {"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
      {"vertex_colors", {{1, 0, 0, 1}, {0, 1, 0, 0.5}, {0, 0, 1, 0}}}};
  meshDescriptor->parse(inlineColors, meshEntity);
  const auto *coloredMesh =
      meshEntity.component<runtime::MeshRendererComponent>();
  const auto &colorProperty = meshSchema["properties"]["vertex_colors"];
  if (!runtime::scene_loading::validateComponent(*meshDescriptor, inlineColors)
           .empty() ||
      meshDescriptor->serialize(meshEntity) != inlineColors ||
      coloredMesh->vertexColors.size() != 3 ||
      coloredMesh->vertexColors[1].g != 1 ||
      coloredMesh->vertexColors[1].a != 0.5F ||
      !runtime::runtimeFieldJson(coloredMesh->vertexColors, encoded) ||
      encoded != inlineColors["vertex_colors"] ||
      meshDefaults["vertex_colors"] != nlohmann::json::array() ||
      colorProperty["items"]["minItems"] != 4 ||
      colorProperty["items"]["maxItems"] != 4 ||
      colorProperty["items"]["items"]["minimum"] != 0 ||
      colorProperty["items"]["items"]["maximum"] != 1 ||
      meshDescriptor->exposedToLua) {
    std::cerr << "Inline mesh colors did not round-trip through the native "
                 "contract.\n";
    return 1;
  }
  for (const nlohmann::json &valid :
       {nlohmann::json::object(),
        nlohmann::json{{"vertex_colors", nlohmann::json::array()}}}) {
    runtime::Entity uncolored;
    meshDescriptor->parse(valid, uncolored);
    if (!uncolored.component<runtime::MeshRendererComponent>()
             ->vertexColors.empty() ||
        !runtime::scene_loading::validateComponent(*meshDescriptor, valid)
             .empty()) {
      std::cerr
          << "Omitted or empty vertex colors must retain white vertex tint.\n";
      return 1;
    }
  }
  std::vector<nlohmann::json> invalidColors{nullptr,
                                            "red",
                                            nlohmann::json::object(),
                                            {{1, 0, 0}},
                                            {{1, 0, 0, 1, 0}},
                                            {{1, 0, "blue", 1}},
                                            {{1, 0, 0, 1}}};
  for (double channel :
       {-0.01, 1.01, 1.000000001, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    auto colors = inlineColors["vertex_colors"];
    colors[1][3] = channel;
    invalidColors.push_back(std::move(colors));
  }
  for (const auto &colors : invalidColors) {
    auto invalid = inlineColors;
    invalid["vertex_colors"] = colors;
    bool rejected = false;
    try {
      meshDescriptor->parse(invalid, meshEntity);
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected ||
        runtime::scene_loading::validateComponent(*meshDescriptor, invalid)
            .empty() ||
        meshDescriptor->serialize(meshEntity) != inlineColors) {
      std::cerr << "Invalid vertex colors were accepted or replaced the "
                   "previous mesh.\n";
      return 1;
    }
  }
  const nlohmann::json withoutVertices = {
      {"vertex_colors", inlineColors["vertex_colors"]}};
  if (runtime::scene_loading::validateComponent(*meshDescriptor,
                                                withoutVertices)
          .empty()) {
    std::cerr << "Nonempty vertex colors require inline vertices.\n";
    return 1;
  }
  auto malformedGeometry = inlineColors;
  malformedGeometry["vertices"] =
      nlohmann::json::array({nullptr, nullptr, nullptr});
  bool rejectedMalformedGeometry = false;
  try {
    runtime::MeshRendererComponent::parse(malformedGeometry, meshEntity);
  } catch (const std::exception &) {
    rejectedMalformedGeometry = true;
  }
  if (!rejectedMalformedGeometry) {
    std::cerr
        << "Vertex colors were accepted without parsed inline vertices.\n";
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

  std::string error;
  const std::optional<runtime::LoadedProject> loaded = runtime::loadProject(
      root / "examples" / "minimal_2d_android" / "demi.project.json", error);
  if (!loaded.has_value()) {
    std::cerr << "Failed to load minimal_2d_android project: " << error << '\n';
    return 1;
  }

  const std::optional<runtime::LoadedProject> chess = runtime::loadProject(
      root / "examples" / "chess" / "demi.project.json", error);
  const std::optional<runtime::LoadedProject> streaming = runtime::loadProject(
      root / "examples" / "asset_streaming_showcase" / "demi.project.json",
      error);
  if (!chess || chess->project.preloadedAssets.size() != 12 || !streaming ||
      !streaming->project.preloadedAssets.empty()) {
    std::cerr << "Project loader did not preserve preloaded assets.\n";
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
  for (const runtime::ui::UiNode &node : loaded->world.ui.nodes) {
    if (node.id == "menu_button_network") {
      foundNetworkButton = node.action == "menu_button_network";
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
    "format_version": 1,
    "canvas_size": [960.0, 540.0],
    "root": {
      "id": "ui_root",
      "type": "container",
      "anchor_min": [0.0, 0.0],
      "anchor_max": [1.0, 1.0],
      "children": [{
        "type": "label",
        "id": "hud_text_font_size",
        "text": "Readable",
        "position": [12.0, 16.0],
        "font_size": 32.0
      },
      {
        "type": "button",
        "id": "hud_button_font_size",
        "text": "OK",
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
      }]
    }
  })json")) {
    std::cerr << "Failed to write HUD parser fixture.\n";
    return 1;
  }
  runtime::World hudWorld;
  runtime::scene_loading::loadHudFile(hudWorld, hudFixture, error);
  bool foundFontSizeText = false;
  bool foundFontSizeButton = false;
  bool foundPanel = false;
  bool foundCircle = false;
  for (const runtime::ui::UiNode &node : hudWorld.ui.nodes) {
    if (node.id == "hud_text_font_size") {
      foundFontSizeText = node.fontSize == 32.0F;
    } else if (node.id == "hud_button_font_size") {
      foundFontSizeButton = node.fontSize == 24.0F;
    } else if (node.id == "hud_panel") {
      foundPanel = node.cornerRadius == 12.0F && node.borderWidth == 2.0F;
    } else if (node.id == "hud_circle") {
      foundCircle = node.radius == 28.0F;
    }
  }
  if (!foundFontSizeText || !foundFontSizeButton || !foundPanel ||
      !foundCircle) {
    std::cerr
        << "HUD loader did not read panel, circle, or font_size fields.\n";
    return 1;
  }
  if (hudWorld.ui.nodes.empty()) {
    std::cerr << "HUD loader did not populate the tree UI representation.\n";
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
            "model": "high",
            "medium_lod_model": "medium",
            "medium_lod_distance": 35.0,
            "low_lod_model": "low",
            "low_lod_distance": 90.0,
            "cull_distance": 220.0,
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
      mesh->component<MeshRendererComponent>()->mediumLodModel != "medium" ||
      mesh->component<MeshRendererComponent>()->mediumLodDistance != 35.0F ||
      mesh->component<MeshRendererComponent>()->lowLodModel != "low" ||
      mesh->component<MeshRendererComponent>()->lowLodDistance != 90.0F ||
      mesh->component<MeshRendererComponent>()->cullDistance != 220.0F ||
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
    "display": {"vsync": false},
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
      gameplayProject->project.display.vsync || gameplayData == nullptr ||
      gameplayData->find("\"current\":50") == std::string::npos ||
      !gameplay->hasComponent<GameplayDataComponent>() ||
      !gameplay->hasComponent<Transform2DComponent>()) {
    std::cerr
        << "Scene loader did not preserve generic gameplay component data.\n";
    return 1;
  }

  return 0;
}
