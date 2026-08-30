#include "demi/runtime/scene/ProjectBuildSettings.h"
#include "demi/runtime/scene/ProjectBuildValidation.h"

#include "demi/assets/AssetRegistry.h"

#include <algorithm>
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

  return 0;
}
