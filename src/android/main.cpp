#include "demi/runtime/app/RuntimeApp.h"

#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

extern "C" android_app* GetAndroidApp(void);

extern "C" ANativeActivity* DemiGetNativeActivity(void) {
  android_app* app = GetAndroidApp();
  return app != nullptr ? app->activity : nullptr;
}

namespace {

constexpr const char* LogTag = "DemiEngine";
constexpr const char* ProjectDirectory = "project";
constexpr const char* ProjectFile = "project/demi.project.json";
constexpr const char* AssetIndexFile = "demi_asset_index.txt";
std::atomic_bool ApplicationSuspended{false};
std::atomic_uint LowMemorySignals{0};
void (*OriginalPause)(ANativeActivity *) = nullptr;
void (*OriginalResume)(ANativeActivity *) = nullptr;
void (*OriginalLowMemory)(ANativeActivity *) = nullptr;

void onPause(ANativeActivity *activity) {
  ApplicationSuspended.store(true, std::memory_order_release);
  if (OriginalPause != nullptr)
    OriginalPause(activity);
}

void onResume(ANativeActivity *activity) {
  ApplicationSuspended.store(false, std::memory_order_release);
  if (OriginalResume != nullptr)
    OriginalResume(activity);
}

void onLowMemory(ANativeActivity *activity) {
  LowMemorySignals.fetch_add(1, std::memory_order_release);
  if (OriginalLowMemory != nullptr)
    OriginalLowMemory(activity);
}

void installLifecycleBridge(ANativeActivity *activity) {
  if (activity == nullptr || activity->callbacks == nullptr)
    return;
  OriginalPause = activity->callbacks->onPause;
  OriginalResume = activity->callbacks->onResume;
  OriginalLowMemory = activity->callbacks->onLowMemory;
  activity->callbacks->onPause = onPause;
  activity->callbacks->onResume = onResume;
  activity->callbacks->onLowMemory = onLowMemory;
}

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

void clearBundledProjectFiles(const std::filesystem::path& projectRoot) {
  std::error_code error;
  std::filesystem::remove(projectRoot / "demi.project.json", error);
  error.clear();
  std::filesystem::remove_all(projectRoot / "assets", error);
  error.clear();
  std::filesystem::remove_all(projectRoot / "scenes", error);
  error.clear();
  std::filesystem::remove_all(projectRoot / "scripts", error);
  error.clear();
  std::filesystem::remove_all(projectRoot / "certs", error);
}

std::filesystem::path prepareProject(android_app* app) {
  const std::filesystem::path storageRoot = app->activity->internalDataPath;
  const std::filesystem::path projectRoot = storageRoot / ProjectDirectory;
  AAssetManager* manager = app->activity->assetManager;

  const std::vector<std::string> assetPaths = readAssetIndex(manager);
  if (assetPaths.empty()) {
    logError("Android asset index is empty.");
  }
  clearBundledProjectFiles(projectRoot);
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

extern "C" bool DemiAndroidApplicationSuspended(void) {
  return ApplicationSuspended.load(std::memory_order_acquire);
}

extern "C" unsigned DemiAndroidConsumeLowMemorySignals(void) {
  return LowMemorySignals.exchange(0, std::memory_order_acq_rel);
}

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
  installLifecycleBridge(app->activity);
  logInfo("Launching " + std::string(ProjectFile));
  return demi::runtime::runProject(demi::runtime::RuntimeOptions{
      .projectPath = projectPath,
  });
}
