#include "editor/EditorProjectOperations.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

int main() {
  namespace fs = std::filesystem;

  demi::editor::EditorProjectOperations operations;
  std::string error;
  assert(operations.start(
      {.operation = demi::build::ProjectOperation::PackageAndroid,
       .projectFile = fs::path(DEMI_SOURCE_DIR) /
                      "examples/minimal_2d_android/demi.project.json",
       .engineRoot = DEMI_SOURCE_DIR},
      error));

  demi::editor::EditorProjectOperationSnapshot snapshot;
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    snapshot = operations.snapshot();
  } while (snapshot.running);

  assert(snapshot.result && snapshot.result->succeeded());
  assert(snapshot.progress.fraction == 1.0F);
  assert(fs::is_regular_file(snapshot.result->artifact));
  assert(snapshot.result->artifact ==
         fs::path(DEMI_SOURCE_DIR) /
             "examples/minimal_2d_android/build/android/minimal_2d_android-debug.apk");
  const fs::path reportPath = snapshot.result->artifact.string() +
                              ".build-report.json";
  assert(fs::is_regular_file(reportPath));
  std::ifstream reportInput(reportPath);
  const nlohmann::json report = nlohmann::json::parse(reportInput);
  assert(report["format_version"] == 1);
  assert(report["configuration"] == "debug");
  assert(report["application_id"] == "dev.jeapi.demi.minimal_2d_android");
  assert(report["artifact_hash"].get<std::string>().starts_with("fnv1a64:"));
  return 0;
}
