#include "demi/runtime/scene/SceneData.h"

#include <filesystem>
#include <iostream>
#include <optional>

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

  return 0;
}
