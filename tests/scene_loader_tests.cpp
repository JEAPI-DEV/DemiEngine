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
      }
    ]
  })json")) {
    std::cerr << "Failed to write HUD parser fixture.\n";
    return 1;
  }
  runtime::World hudWorld;
  runtime::scene_loading::loadHudFile(hudWorld, hudFixture, error);
  if (hudWorld.hudText.empty() || hudWorld.hudText[0].fontSize != 32.0F || hudWorld.hudButtons.empty() || hudWorld.hudButtons[0].fontSize != 24.0F) {
    std::cerr << "HUD loader did not read font_size fields.\n";
    return 1;
  }

  const std::filesystem::path voxelFixture = std::filesystem::temp_directory_path() / "demi_scene_loader_voxel_fixture";
  std::error_code fsError;
  std::filesystem::remove_all(voxelFixture, fsError);
  if (!writeFile(voxelFixture / "demi.project.json", R"json({
    "format_version": 1,
    "name": "Voxel Fixture",
    "main_scene": "scene://fixture/main",
    "scenes": [
      {
        "id": "scene://fixture/main",
        "path": "scenes/main.scene.json"
      }
    ]
  })json") ||
      !writeFile(voxelFixture / "assets" / "blocksets" / "basic.asset.json", R"json({
    "format_version": 1,
    "id": "asset://blocksets/basic",
    "type": "VoxelBlockSet",
    "source": "basic.voxel.json"
  })json") ||
      !writeFile(voxelFixture / "assets" / "blocksets" / "basic.voxel.json", R"json({
    "format_version": 1,
    "tile_size": 16,
    "atlas_sources": [],
    "blocks": [
      {
        "id": 0,
        "name": "air",
        "solid": false,
        "tile": 0
      },
      {
        "id": 1,
        "name": "grass",
        "solid": true,
        "tile": 0
      },
      {
        "id": 2,
        "name": "dirt",
        "solid": true,
        "tile": 0
      },
      {
        "id": 3,
        "name": "stone",
        "solid": true,
        "tile": 0
      }
    ]
  })json") ||
      !writeFile(voxelFixture / "scenes" / "main.scene.json", R"json({
    "format_version": 1,
    "id": "scene://fixture/main",
    "name": "Voxel Fixture Scene",
    "entities": [
      {
        "id": "ent_chunk_0_0",
        "name": "Terrain Chunk",
        "components": {
          "Transform3D": {
            "position": [0.0, 0.0, 0.0]
          },
          "VoxelChunk": {
            "block_set": "asset://blocksets/basic",
            "dimensions": [16, 12, 16],
            "terrain": {
              "seed": 4242,
              "base_height": 4,
              "amplitude": 3,
              "frequency": 0.11,
              "dirt_depth": 3,
              "surface_block": "grass",
              "subsurface_block": "dirt",
              "stone_block": "stone"
            }
          }
        }
      }
    ]
  })json")) {
    std::cerr << "Failed to write voxel scene loader fixture.\n";
    return 1;
  }

  const std::optional<runtime::LoadedProject> voxel = runtime::loadProject(voxelFixture / "demi.project.json", error);
  if (!voxel.has_value()) {
    std::cerr << "Failed to load voxel fixture project: " << error << '\n';
    return 1;
  }

  const runtime::Entity* chunk = runtime::findEntity(voxel->world, "ent_chunk_0_0");
  if (chunk == nullptr || !chunk->voxelChunk.has_value() || chunk->voxelChunk->blockSet != "asset://blocksets/basic" || !chunk->voxelChunk->terrain.enabled) {
    std::cerr << "Scene loader did not read VoxelChunk component data.\n";
    return 1;
  }

  return 0;
}
