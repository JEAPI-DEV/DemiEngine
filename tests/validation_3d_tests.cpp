#include "demi/schema/Validation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool hasCode(const demi::Diagnostics &diagnostics, const std::string &code) {
  return std::ranges::any_of(diagnostics, [&](const demi::Diagnostic &entry) {
    return entry.code == code;
  });
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_validation_3d_" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  const auto scene = root / "hierarchy.scene.json";
  {
    std::ofstream output(scene);
    output << R"({"format_version":1,"id":"scene://test","entities":[
      {"id":"a","components":{"Transform3D":{"parent":"b"}}},
      {"id":"b","components":{"Transform3D":{"parent":"a"}}},
      {"id":"missing_child","components":{"Transform3D":{"parent":"gone"}}}
    ]})";
  }
  const auto diagnostics =
      demi::validateTextFile(scene, demi::SourceFileKind::Scene);
  std::filesystem::remove_all(root);
  if (!hasCode(diagnostics, "TRANSFORM3D_HIERARCHY_CYCLE") ||
      !hasCode(diagnostics, "TRANSFORM3D_PARENT_NOT_FOUND")) {
    std::cerr << "3D hierarchy validation diagnostics were not emitted.\n";
    return 1;
  }
  return 0;
}
