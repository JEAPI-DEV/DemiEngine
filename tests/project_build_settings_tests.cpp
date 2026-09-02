#include "demi/capabilities/PlatformCapabilities.h"
#include "demi/runtime/scene/ProjectBuildSettings.h"
#include "demi/runtime/scene/ProjectBuildValidation.h"

#include "demi/assets/AssetRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace {

bool containsCode(const demi::Diagnostics &diagnostics, std::string_view code) {
  return std::ranges::any_of(diagnostics, [code](const auto &diagnostic) {
    return diagnostic.code == code;
  });
}

} // namespace

int main() {
  const nlohmann::json valid = nlohmann::json::parse(R"({
    "format_version": 1,
    "name": "Shipping Test",
    "build": {
      "application_id": "dev.jeapi.shipping_test",
      "display_name": "Shipping Test",
      "executable_name": "shipping_test",
      "version_name": "1.2.3",
      "version_code": 7,
      "icon": "asset://branding/icon",
      "splash": "asset://branding/splash",
      "window": {"width": 1600, "height": 900, "mode": "borderless"},
      "android": {
        "orientation": "landscape_sensor",
        "min_sdk": 28,
        "abis": ["arm64-v8a", "x86_64"],
        "permissions": ["android.permission.INTERNET"]
      }
    }
  })");

  const auto parsed =
      demi::runtime::parseProjectBuildSettings(valid, "demi.project.json");
  if (demi::hasErrors(parsed.diagnostics) || !parsed.settings.authored ||
      parsed.settings.applicationId != "dev.jeapi.shipping_test" ||
      parsed.settings.window.width != 1600 ||
      parsed.settings.android.minimumSdk != 28 ||
      parsed.settings.android.abis.size() != 2) {
    std::cerr << "Valid project build settings did not parse correctly.\n";
    return 1;
  }

  const nlohmann::json canonical =
      demi::runtime::projectBuildSettingsJson(parsed.settings);
  if (canonical["version_code"] != 7 ||
      canonical["android"]["orientation"] != "landscape_sensor" ||
      canonical["window"]["mode"] != "borderless") {
    std::cerr << "Canonical build settings lost parsed values.\n";
    return 1;
  }

  const auto defaults = demi::runtime::parseProjectBuildSettings(
      nlohmann::json{{"name", "My Game"}}, "demi.project.json");
  if (demi::hasErrors(defaults.diagnostics) || defaults.settings.authored ||
      defaults.settings.displayName != "My Game" ||
      defaults.settings.executableName != "my_game") {
    std::cerr << "Unauthored build settings defaults are incorrect.\n";
    return 1;
  }

  nlohmann::json invalid = valid;
  invalid["build"]["application_id"] = "Not Reverse DNS";
  invalid["build"]["version_name"] = "release";
  invalid["build"]["window"]["width"] = "wide";
  invalid["build"]["android"]["abis"] = {"armeabi-v7a"};
  invalid["build"]["android"]["permissions"] = {
      "INTERNET", "INTERNET"};
  invalid["build"]["unexpected"] = true;
  const auto rejected =
      demi::runtime::parseProjectBuildSettings(invalid, "demi.project.json");

  for (const std::string_view code : {
           "PROJECT_BUILD_APPLICATION_ID_INVALID",
           "PROJECT_BUILD_VERSION_NAME_INVALID",
           "PROJECT_BUILD_FIELD_TYPE_INVALID",
           "PROJECT_BUILD_ABI_INVALID",
           "PROJECT_BUILD_PERMISSION_INVALID",
           "PROJECT_BUILD_FIELD_UNKNOWN"}) {
    if (!containsCode(rejected.diagnostics, code)) {
      std::cerr << "Missing expected diagnostic: " << code << '\n';
      return 1;
    }
  }

  demi::AssetRegistry emptyRegistry;
  const auto missingBranding = demi::runtime::validateProjectBuildAssets(
      parsed.settings, emptyRegistry, "demi.project.json");
  if (!containsCode(missingBranding, "PROJECT_BUILD_ASSET_NOT_FOUND")) {
    std::cerr << "Missing branding assets were not rejected.\n";
    return 1;
  }

  const auto &host = demi::capabilities::fullyConfiguredRuntimeFeatures();
  if (!(demi::capabilities::targetRuntimeFeatures(
            demi::capabilities::TargetPlatform::Linux, host) == host)) {
    std::cerr << "Linux target should use the host renderer features.\n";
    return 1;
  }
  const auto androidFeatures = demi::capabilities::targetRuntimeFeatures(
      demi::capabilities::TargetPlatform::Android, host);
  if (!androidFeatures.network || androidFeatures.media ||
      androidFeatures.svg || !androidFeatures.graphicsRuntime) {
    std::cerr << "Android target profile is incorrect.\n";
    return 1;
  }
  const auto serverFeatures = demi::capabilities::targetRuntimeFeatures(
      demi::capabilities::TargetPlatform::LinuxServer, host);
  if (serverFeatures.graphicsRuntime || serverFeatures.svg ||
      !serverFeatures.network || !serverFeatures.media) {
    std::cerr << "Linux server target profile is incorrect.\n";
    return 1;
  }

  const std::filesystem::path fixtureRoot =
      std::filesystem::temp_directory_path() / "demi_build_settings_fixture";
  std::filesystem::remove_all(fixtureRoot);
  const auto writeFile = [&fixtureRoot](const std::string_view relative,
                                        const std::string_view contents) {
    const auto path = fixtureRoot / std::filesystem::path(relative);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
  };
  writeFile(
      "demi.project.json",
      R"({
        "format_version": 1,
        "name": "Capability Fixture",
        "network_contract": "asset://network/fixture",
        "assets": ["asset://art/logo"],
        "build": {
          "application_id": "dev.jeapi.capability_fixture",
          "display_name": "Capability Fixture",
          "android": {"permissions": []}
        }
      })");
  writeFile("assets/network/fixture.asset.json",
            R"({
              "format_version": 1,
              "id": "asset://network/fixture",
              "type": "NetworkContract",
              "importer": "network_contract",
              "importer_version": 1,
              "source": "fixture.network.json",
              "source_hash": "fnv1a64:test",
              "dependencies": []
            })");
  writeFile("assets/art/logo.asset.json",
            R"({
              "format_version": 1,
              "id": "asset://art/logo",
              "type": "SvgTexture2D",
              "importer": "svg",
              "importer_version": 1,
              "source": "logo.svg",
              "source_hash": "fnv1a64:test",
              "dependencies": []
            })");
  writeFile("assets/media/intro.asset.json",
            R"({
              "format_version": 1,
              "id": "asset://media/intro",
              "type": "VideoClip",
              "importer": "video",
              "importer_version": 1,
              "source": "intro.mp4",
              "source_hash": "fnv1a64:test",
              "dependencies": []
            })");
  writeFile("scenes/main.scene.json",
            R"({
              "format_version": 1,
              "id": "scene://fixture/main",
              "entities": [{
                "id": "ent_video",
                "components": {"VideoPlayer": {"asset": "asset://media/intro"}}
              }, {
                "id": "ent_logo",
                "components": {"Sprite": {"texture": "asset://art/logo"}}
              }]
            })");
  writeFile("scripts/game.lua",
            R"(local handled = Network.is_host()
            if handled then
              Video.play("asset://media/intro")
            end)");

  const auto fixture = nlohmann::json::parse(
      std::ifstream{fixtureRoot / "demi.project.json"});
  const auto registry = demi::loadAssetRegistry(fixtureRoot);
  const auto usage = demi::runtime::scanProjectFeatureUsage(
      fixture, fixtureRoot, registry);
  if (!usage.network || !usage.media || !usage.svg ||
      usage.networkEvidence.empty() || usage.mediaEvidence.empty() ||
      usage.svgEvidence.empty()) {
    std::cerr << "Feature usage scan missed a reachable feature.\n";
    return 1;
  }

  const auto fixtureParsed = demi::runtime::parseProjectBuildSettings(
      fixture, fixtureRoot / "demi.project.json");
  if (demi::hasErrors(fixtureParsed.diagnostics)) {
    std::cerr << "Capability fixture build settings did not parse.\n";
    return 1;
  }

  const auto rejectedOnAndroid = demi::runtime::validateProjectPlatformCapabilities(
      fixtureParsed.settings, usage, demi::capabilities::TargetPlatform::Android,
      androidFeatures, fixtureRoot / "demi.project.json");
  for (const std::string_view code : {
           "PROJECT_BUILD_FEATURE_MEDIA_UNSUPPORTED",
           "PROJECT_BUILD_FEATURE_SVG_UNSUPPORTED",
           "PROJECT_BUILD_PERMISSION_NETWORK_MISSING"}) {
    if (!containsCode(rejectedOnAndroid, code)) {
      std::cerr << "Missing expected capability diagnostic: " << code << '\n';
      return 1;
    }
  }

  auto declaredInternet = fixture;
  declaredInternet["build"]["android"]["permissions"] = {
      "android.permission.INTERNET"};
  const auto internetParsed = demi::runtime::parseProjectBuildSettings(
      declaredInternet, fixtureRoot / "demi.project.json");
  const auto internetOnAndroid = demi::runtime::validateProjectPlatformCapabilities(
      internetParsed.settings, usage, demi::capabilities::TargetPlatform::Android,
      androidFeatures, fixtureRoot / "demi.project.json");
  if (containsCode(internetOnAndroid, "PROJECT_BUILD_PERMISSION_NETWORK_MISSING")) {
    std::cerr << "Declared INTERNET permission was still reported missing.\n";
    return 1;
  }

  auto legacyProject = nlohmann::json::parse(std::ifstream{fixtureRoot /
                                                           "demi.project.json"});
  legacyProject.erase("build");
  const auto legacyParsed = demi::runtime::parseProjectBuildSettings(
      legacyProject, fixtureRoot / "demi.project.json");
  if (demi::hasErrors(legacyParsed.diagnostics) || legacyParsed.settings.authored) {
    std::cerr << "Legacy project without a build block did not migrate.\n";
    return 1;
  }
  const auto legacyOnAndroid = demi::runtime::validateProjectPlatformCapabilities(
      legacyParsed.settings, usage, demi::capabilities::TargetPlatform::Android,
      androidFeatures, fixtureRoot / "demi.project.json");
  if (containsCode(legacyOnAndroid, "PROJECT_BUILD_PERMISSION_NETWORK_MISSING")) {
    std::cerr << "Legacy project without a build block must keep using the "
                 "Gradle default INTERNET permission.\n";
    return 1;
  }

  const nlohmann::json partialBlock = nlohmann::json::parse(R"({
    "format_version": 1,
    "name": "Partial Fixture",
    "build": {"application_id": "dev.jeapi.partial_fixture"}
  })");
  const auto partialParsed =
      demi::runtime::parseProjectBuildSettings(partialBlock, "demi.project.json");
  if (demi::hasErrors(partialParsed.diagnostics) ||
      !partialParsed.settings.authored ||
      partialParsed.settings.versionName != "0.1.0" ||
      partialParsed.settings.versionCode != 1 ||
      partialParsed.settings.window.width != 1280 ||
      partialParsed.settings.window.mode != "windowed" ||
      partialParsed.settings.android.minimumSdk != 26 ||
      partialParsed.settings.android.orientation != "unspecified" ||
      partialParsed.settings.android.abis.size() != 1 ||
      partialParsed.settings.android.abis.front() != "arm64-v8a" ||
      !partialParsed.settings.android.permissions.empty()) {
    std::cerr << "Partial legacy build block did not migrate per field.\n";
    return 1;
  }

  auto offlineHost = host;
  offlineHost.network = false;
  const auto rejectedOnLinux = demi::runtime::validateProjectPlatformCapabilities(
      fixtureParsed.settings, usage, demi::capabilities::TargetPlatform::Linux,
      offlineHost, fixtureRoot / "demi.project.json");
  if (!containsCode(rejectedOnLinux,
                    "PROJECT_BUILD_FEATURE_NETWORK_UNSUPPORTED")) {
    std::cerr << "Linux runtime without networking did not reject network "
                 "content.\n";
    return 1;
  }

  const auto acceptedOnLinux = demi::runtime::validateProjectPlatformCapabilities(
      fixtureParsed.settings, usage, demi::capabilities::TargetPlatform::Linux,
      host, fixtureRoot / "demi.project.json");
  if (demi::hasErrors(acceptedOnLinux)) {
    std::cerr << "Fully configured Linux target reported capability errors.\n";
    return 1;
  }

  const auto convenience = demi::runtime::validateProjectPlatformCapabilities(
      fixtureRoot / "demi.project.json",
      demi::capabilities::TargetPlatform::Android, host);
  if (!containsCode(convenience, "PROJECT_BUILD_FEATURE_SVG_UNSUPPORTED")) {
    std::cerr << "Path-based capability entry point did not report SVG "
                 "restriction.\n";
    return 1;
  }

  // Branding-only SVG assets are rasterized or installed by the packager,
  // so they must not require runtime SVG support.
  const std::filesystem::path brandingRoot =
      std::filesystem::temp_directory_path() / "demi_build_settings_branding";
  std::filesystem::remove_all(brandingRoot);
  std::filesystem::create_directories(brandingRoot / "assets/art");
  std::filesystem::copy_file(fixtureRoot / "assets/art/logo.asset.json",
                             brandingRoot / "assets/art/logo.asset.json");
  {
    std::ofstream output(brandingRoot / "demi.project.json");
    output << R"({
      "format_version": 1,
      "name": "Branding Fixture",
      "build": {
        "application_id": "dev.jeapi.branding_fixture",
        "display_name": "Branding Fixture",
        "icon": "asset://art/logo"
      }
    })";
  }
  const auto brandingProject = nlohmann::json::parse(
      std::ifstream{brandingRoot / "demi.project.json"});
  const auto brandingRegistry = demi::loadAssetRegistry(brandingRoot);
  const auto brandingUsage =
      demi::runtime::scanProjectFeatureUsage(brandingProject, brandingRoot,
                                             brandingRegistry);
  if (brandingUsage.svg || brandingUsage.media || brandingUsage.network) {
    std::cerr << "Branding-only assets were treated as runtime feature "
                 "dependencies.\n";
    return 1;
  }
  const auto brandingParsed = demi::runtime::parseProjectBuildSettings(
      brandingProject, brandingRoot / "demi.project.json");
  const auto brandingOnAndroid =
      demi::runtime::validateProjectPlatformCapabilities(
          brandingParsed.settings, brandingUsage,
          demi::capabilities::TargetPlatform::Android, androidFeatures,
          brandingRoot / "demi.project.json");
  if (containsCode(brandingOnAndroid,
                   "PROJECT_BUILD_FEATURE_SVG_UNSUPPORTED")) {
    std::cerr << "SVG branding assets must not fail Android packaging.\n";
    return 1;
  }

  std::filesystem::remove_all(fixtureRoot);
  std::filesystem::remove_all(brandingRoot);
  return 0;
}
