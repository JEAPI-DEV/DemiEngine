#include "demi/runtime/scene/ComponentRegistry.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  try {
    std::ifstream input(DEMI_SOURCE_DIR "/schemas/components.schema.json");
    if (!input) {
      std::cerr << "Could not read checked-in component schema.\n";
      return 1;
    }
    const auto checkedIn = nlohmann::json::parse(input);
    const auto generated =
        demi::runtime::scene_loading::canonicalComponentSchema();
    if (checkedIn != generated) {
      std::cerr << "Component schema differs from the runtime registry. "
                   "Regenerate it with demi schema export.\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Component schema generation failed: " << error.what() << '\n';
    return 1;
  }
}
