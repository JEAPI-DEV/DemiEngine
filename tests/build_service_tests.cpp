#include "cli/build/BuildService.h"
#include "cli/build/PackageContentAudit.h"

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

  // The package-content audit rejects non-runtime content and records
  // deterministic per-root file counts for the archived build report.
  const auto audit = demi::build::auditPackagedProject(cooked);
  assert(audit.unexpected.empty());
  assert(audit.fileCounts.contains("scenes") &&
         audit.fileCounts.at("scenes") == 1);
  assert(audit.fileCounts.contains("demi.project.json") &&
         audit.fileCounts.at("demi.project.json") == 1);
  assert(fs::is_regular_file(bundle / "build-report.json"));
  demi::build::stripCookCache(cooked);

  const fs::path leaky = root / "output/leaky-cooked";
  fs::create_directories(leaky);
  if (fs::exists(cooked / "assets"))
    fs::copy(cooked / "assets", leaky / "assets",
             fs::copy_options::recursive);
  for (const char *allowed : {"scenes", "scripts", "certs", "packages"}) {
    if (fs::exists(cooked / allowed))
      fs::copy(cooked / allowed, leaky / allowed, fs::copy_options::recursive);
  }
  fs::copy_file(cooked / "demi.project.json", leaky / "demi.project.json",
                fs::copy_options::overwrite_existing);
  fs::copy_file(cooked / "cook.manifest.json", leaky / "cook.manifest.json",
                fs::copy_options::overwrite_existing);
  write(leaky / "saves/leaked.save.json", "{}");
  write(leaky / ".cook-cache/junk.bin", "junk");
  const auto leakAudit = demi::build::auditPackagedProject(leaky);
  assert(leakAudit.unexpected.size() == 2);
  assert(std::ranges::any_of(leakAudit.unexpected, [](const fs::path &path) {
    return path.filename() == "saves";
  }));
  assert(std::ranges::any_of(leakAudit.unexpected, [](const fs::path &path) {
    return path.filename() == ".cook-cache";
  }));
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
