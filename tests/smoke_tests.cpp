#include "demi/diagnostics/Diagnostic.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace {

bool verifyGameplayHudContract(const std::filesystem::path &example) {
  const std::filesystem::path hudPath = example / "scenes/game.hud.json";
  std::ifstream hudInput(hudPath);
  const nlohmann::json document = nlohmann::json::parse(hudInput);

  bool gameplayVisible = false;
  bool gameOverHidden = false;
  bool hasTouchControls = false;
  bool touchControlsHidden = true;
  for (const nlohmann::json &node : document.at("root").at("children")) {
    if (node.value("id", "") == "game_hud") {
      gameplayVisible = node.value("visible", true);
      for (const nlohmann::json &child :
           node.value("children", nlohmann::json::array())) {
        const std::string type = child.value("type", "");
        if (type == "virtual_button" || type == "virtual_stick") {
          hasTouchControls = true;
          touchControlsHidden =
              touchControlsHidden && !child.value("visible", true);
        }
      }
    }
    if (node.value("id", "") == "game_over")
      gameOverHidden = !node.value("visible", true);
  }

  std::ifstream viewInput(example / "scripts/menu/view.lua");
  std::ostringstream view;
  view << viewInput.rdbuf();
  const bool togglesGameplayParent =
      view.str().find("Hud.set_visible(\"game_hud\", visible)") !=
      std::string::npos;
  const bool platformGatesTouchControls =
      !hasTouchControls ||
      (view.str().find("Application.platform() == \"android\"") !=
           std::string::npos &&
       view.str().find("Hud.set_visible(\"touch_move\", visible and "
                       "touch_controls_enabled)") != std::string::npos &&
       view.str().find("Hud.set_visible(\"touch_jump\", visible and "
                       "touch_controls_enabled)") != std::string::npos);

  if (gameplayVisible && gameOverHidden && togglesGameplayParent &&
      touchControlsHidden && platformGatesTouchControls)
    return true;

  std::cerr << example.string()
            << ": gameplay HUD must start visible, game-over UI must start "
               "hidden, and menu transitions must toggle the structural "
               "game_hud parent. Virtual controls must default hidden and "
               "be enabled only on Android.\n";
  return false;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1])
                                              : std::filesystem::current_path();
  const demi::ValidationSummary summary = demi::validatePath(root / "examples");
  if (demi::hasErrors(summary.diagnostics)) {
    demi::printDiagnosticsText(std::cerr, summary.diagnostics);
    return 1;
  }

  if (!verifyGameplayHudContract(root / "examples/minimal_2d_android") ||
      !verifyGameplayHudContract(root / "examples/minimal_2d_networking"))
    return 1;

  std::cout << "Smoke validation checked " << summary.checkedFiles
            << " file(s).\n";
  return 0;
}
