#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/scene/HudParser.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

namespace {

bool writeFile(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << contents;
  return true;
}

} // namespace

int main(int argc, char** argv) {
  namespace runtime = demi::runtime;

  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();

  std::string error;
  const std::optional<runtime::LoadedProject> loaded = runtime::loadProject(root / "examples" / "minimal_2d_networking" / "demi.project.json", error);
  if (!loaded.has_value()) {
    std::cerr << "Failed to load minimal_2d_networking project: " << error << '\n';
    return 1;
  }

  const runtime::Entity* camera = runtime::findEntity(loaded->world, "ent_camera_menu");
  if (camera == nullptr || !camera->transform2D.has_value() || !camera->camera2D.has_value()) {
    std::cerr << "Scene loader did not read nested camera components.\n";
    return 1;
  }

  const runtime::Entity* controller = runtime::findEntity(loaded->world, "ent_menu_controller");
  if (controller == nullptr || !controller->luaScript.has_value() || controller->luaScript->module != "script://scripts/menu_scene.lua") {
    std::cerr << "Scene loader did not read nested LuaScript component.\n";
    return 1;
  }

  bool foundNetworkButton = false;
  for (const runtime::HudButtonElement& button : loaded->world.hudButtons) {
    if (button.id == "menu_button_network") {
      foundNetworkButton = button.action == "menu_button_network";
      break;
    }
  }
  if (!foundNetworkButton) {
    std::cerr << "HUD loader did not preserve menu button action.\n";
    return 1;
  }

  const std::filesystem::path hudFixture = std::filesystem::temp_directory_path() / "demi_scene_loader_hud_fixture.json";
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
  if (hudWorld.hudText.empty() || hudWorld.hudText[0].fontSize != 32.0F || hudWorld.hudButtons.empty() || hudWorld.hudButtons[0].fontSize != 24.0F ||
      hudWorld.hudPanels.empty() || hudWorld.hudPanels[0].cornerRadius != 12.0F || hudWorld.hudPanels[0].borderWidth != 2.0F ||
      hudWorld.hudCircles.empty() || hudWorld.hudCircles[0].radius != 28.0F) {
    std::cerr << "HUD loader did not read panel, circle, or font_size fields.\n";
    return 1;
  }

  const std::filesystem::path meshFixture = std::filesystem::temp_directory_path() / "demi_scene_loader_mesh_fixture";
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

  const std::optional<runtime::LoadedProject> meshProject = runtime::loadProject(meshFixture / "demi.project.json", error);
  if (!meshProject.has_value()) {
    std::cerr << "Failed to load mesh fixture project: " << error << '\n';
    return 1;
  }

  const runtime::Entity* mesh = runtime::findEntity(meshProject->world, "ent_mesh_0");
  if (mesh == nullptr || !mesh->meshRenderer.has_value() || mesh->meshRenderer->vertices.size() != 3 || mesh->meshRenderer->normals.size() != 3 ||
      mesh->meshRenderer->uvs.size() != 3 || mesh->meshRenderer->vertices[1].x != 1.0F || mesh->meshRenderer->uvs[2].y != 1.0F) {
    std::cerr << "Scene loader did not read dynamic MeshRenderer data.\n";
    return 1;
  }

  return 0;
}
