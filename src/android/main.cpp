#include "demi/runtime/app/RuntimeApp.h"

#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

extern "C" android_app* GetAndroidApp(void);

namespace {

constexpr const char* LogTag = "DemiEngine";
constexpr const char* ProjectDirectory = "elite_frontier";
constexpr const char* ProjectFile = "elite_frontier/demi.project.json";
constexpr const char* AssetIndexFile = "demi_asset_index.txt";

void logInfo(const std::string& message) {
  __android_log_print(ANDROID_LOG_INFO, LogTag, "%s", message.c_str());
}

void logError(const std::string& message) {
  __android_log_print(ANDROID_LOG_ERROR, LogTag, "%s", message.c_str());
}

bool copyAssetFile(AAssetManager* manager, const std::string& assetPath, const std::filesystem::path& outputPath) {
  AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_STREAMING);
  if (asset == nullptr) {
    return false;
  }

  std::error_code error;
  std::filesystem::create_directories(outputPath.parent_path(), error);
  if (error) {
    AAsset_close(asset);
    return false;
  }

  std::ofstream output(outputPath, std::ios::binary);
  if (!output) {
    AAsset_close(asset);
    return false;
  }

  std::array<char, 16384> buffer{};
  while (true) {
    const int read = AAsset_read(asset, buffer.data(), buffer.size());
    if (read < 0) {
      AAsset_close(asset);
      return false;
    }
    if (read == 0) {
      break;
    }
    output.write(buffer.data(), read);
  }

  AAsset_close(asset);
  return true;
}

std::vector<std::string> readAssetIndex(AAssetManager* manager) {
  AAsset* asset = AAssetManager_open(manager, AssetIndexFile, AASSET_MODE_BUFFER);
  if (asset == nullptr) {
    return {};
  }

  const off_t length = AAsset_getLength(asset);
  std::string text(static_cast<std::size_t>(std::max<off_t>(length, 0)), '\0');
  if (!text.empty()) {
    const int read = AAsset_read(asset, text.data(), text.size());
    if (read < 0) {
      text.clear();
    } else {
      text.resize(static_cast<std::size_t>(read));
    }
  }
  AAsset_close(asset);

  std::vector<std::string> paths;
  std::istringstream lines(text);
  std::string path;
  while (std::getline(lines, path)) {
    if (!path.empty()) {
      paths.push_back(path);
    }
  }
  return paths;
}

std::filesystem::path prepareProject(android_app* app) {
  const std::filesystem::path storageRoot = app->activity->internalDataPath;
  const std::filesystem::path projectRoot = storageRoot / ProjectDirectory;
  AAssetManager* manager = app->activity->assetManager;

  const std::vector<std::string> assetPaths = readAssetIndex(manager);
  if (assetPaths.empty()) {
    logError("Android asset index is empty.");
  }
  for (const std::string& assetPath : assetPaths) {
    if (!copyAssetFile(manager, assetPath, projectRoot / assetPath)) {
      logError("Failed to copy Android asset: " + assetPath);
    }
  }

  std::error_code error;
  std::filesystem::create_directories(projectRoot / "saves", error);

  return projectRoot / "demi.project.json";
}

} // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  android_app* app = GetAndroidApp();
  if (app == nullptr || app->activity == nullptr || app->activity->assetManager == nullptr ||
      app->activity->internalDataPath == nullptr) {
    logError("Android activity is unavailable.");
    return 1;
  }

  const std::filesystem::path projectPath = prepareProject(app);
  logInfo("Launching " + std::string(ProjectFile));
  return demi::runtime::runProject(demi::runtime::RuntimeOptions{
      .projectPath = projectPath,
  });
}
