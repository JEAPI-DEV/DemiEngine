#include "demi/runtime/scene/ProjectParser.h"
#include "demi/runtime/simulation/DeterministicRandom.h"

#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  using demi::runtime::simulation::DeterministicRandom;
  DeterministicRandom first(42);
  DeterministicRandom second(42);
  for (int index = 0; index < 100; ++index) {
    if (first.nextU32() != second.nextU32()) {
      std::cerr << "Equal deterministic random seeds diverged.\n";
      return 1;
    }
  }
  const std::uint64_t savedState = first.state();
  const float expected = first.value();
  first.restore(savedState);
  if (first.value() != expected || first.integer(7, 7) != 7 ||
      first.range(4.0F, -2.0F) < -2.0F) {
    std::cerr << "Random state restore or range behavior failed.\n";
    return 1;
  }

  std::string error;
  const nlohmann::json projectJson = nlohmann::json::parse(R"({
    "format_version": 1,
    "name": "Simulation Test",
    "main_scene": "scene://main",
    "scenes": [{"id": "scene://main", "path": "main.scene.json"}],
    "simulation": {"fixed_timestep": 0.02, "random_seed": 99}
  })");
  const auto project = demi::runtime::scene_loading::parseProjectData(
      "demi.project.json", projectJson, error);
  if (!project ||
      std::abs(project->simulation.fixedTimestep - 0.02F) > 0.000001F ||
      project->simulation.randomSeed != 99) {
    std::cerr << "Simulation configuration parsing failed: " << error << '\n';
    return 1;
  }
  return 0;
}
