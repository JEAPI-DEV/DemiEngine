#include "cli/build/BuildService.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "demi_build_service_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  write(
      root / "project/demi.project.json",
      R"({"format_version":1,"name":"Build Fixture","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}],"assets":[]})");
  write(root / "project/scenes/main.scene.json",
        R"({"format_version":1,"id":"scene://main","entities":[]})");
  write(root / "bin/demi", "runtime");

  const fs::path project = root / "project/demi.project.json";
  const auto validation = demi::build::runProjectOperation(
      {.operation = demi::build::ProjectOperation::Validate,
       .projectFile = project});
  assert(validation.succeeded());

  const fs::path cooked = root / "output/cooked";
  const auto cook = demi::build::runProjectOperation(
      {.operation = demi::build::ProjectOperation::CookLinux,
       .projectFile = project,
       .outputDirectory = cooked});
  assert(cook.succeeded());
  assert(fs::is_regular_file(cooked / "cook.manifest.json"));

  const fs::path bundle = root / "output/bundle";
  const auto package = demi::build::runProjectOperation(
      {.operation = demi::build::ProjectOperation::PackageLinux,
       .projectFile = project,
       .outputDirectory = bundle,
       .runtimeExecutable = root / "bin/demi"});
  assert(package.succeeded());
  assert(fs::is_regular_file(bundle / "bin/demi"));
  assert(fs::is_regular_file(bundle / "project/demi.project.json"));
  assert(fs::is_regular_file(bundle / "project/cook.manifest.json"));
  assert(readJson(cooked / "cook.manifest.json") ==
         readJson(bundle / "project/cook.manifest.json"));

  const fs::path cancelledOutput = root / "output/cancelled";
  const auto cancelled = demi::build::runProjectOperation(
      {.operation = demi::build::ProjectOperation::CookLinux,
       .projectFile = project,
       .outputDirectory = cancelledOutput,
       .isCancelled = [] { return true; }});
  assert(cancelled.stage == demi::build::ProjectOperationStage::Cancelled);
  assert(!fs::exists(cancelledOutput));

  fs::remove_all(root, ignored);
}
