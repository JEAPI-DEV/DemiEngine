#include "cli/build/BuildService.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include <cstdlib>

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
      R"({"format_version":1,"name":"Build Fixture","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}],"assets":[],"build":{"application_id":"dev.example.build_fixture","display_name":"Build Fixture","executable_name":"build_fixture","version_name":"1.0.0","version_code":1}})");
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
  assert(fs::is_regular_file(bundle / "bin/build_fixture"));
  assert(fs::is_regular_file(bundle / "build_fixture"));
  assert(fs::is_regular_file(bundle / "project/demi.project.json"));
  assert(fs::is_regular_file(bundle / "project/cook.manifest.json"));
  assert(fs::is_regular_file(
      bundle / "share/applications/dev.example.build_fixture.desktop"));
  assert(fs::is_regular_file(
      bundle / "share/doc/build_fixture/THIRD_PARTY_NOTICES.txt"));
  assert(fs::is_regular_file(bundle / "build-report.json"));
  const auto report = readJson(bundle / "build-report.json");
  assert(report["application_id"] == "dev.example.build_fixture");
  assert(report["shared_library_policy"] == "system");
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

  unsetenv("DEMI_ANDROID_KEYSTORE");
  unsetenv("DEMI_ANDROID_KEYSTORE_PASSWORD");
  unsetenv("DEMI_ANDROID_KEY_ALIAS");
  unsetenv("DEMI_ANDROID_KEY_PASSWORD");
  const auto unsignedRelease = demi::build::runProjectOperation(
      {.operation = demi::build::ProjectOperation::BundleAndroidRelease,
       .projectFile = project});
  assert(!unsignedRelease.succeeded());
  assert(std::ranges::any_of(unsignedRelease.diagnostics, [](const auto &item) {
    return item.code == "BUILD_ANDROID_SIGNING_ENVIRONMENT_MISSING";
  }));

  fs::remove_all(root, ignored);
}
