#include "demi/runtime/app/RuntimeApp.h"
#include "demi/runtime/ui/UiAccessibilityBridge.h"
#include "demi/runtime/ui/UiAccessibilityTree.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

extern "C" JNIEXPORT jstring JNICALL
Java_dev_jeapi_demi_android_DemiActivity_nativeAccessibilitySnapshot(
    JNIEnv *environment, jclass) {
  auto &bridge = demi::runtime::ui::platformUiAccessibilityBridge();
  const auto canvas = bridge.canvasSize();
  nlohmann::json output{{"revision", bridge.revision()},
                        {"canvas_width", canvas.x},
                        {"canvas_height", canvas.y},
                        {"nodes", nlohmann::json::array()}};
  for (const auto &node : bridge.snapshot())
    output["nodes"].push_back(
        {{"id", node.id},
         {"parent", node.parent},
         {"role", demi::runtime::ui::uiAccessibilityRoleName(node.role)},
         {"label", node.label},
         {"description", node.description},
         {"value_text", node.valueText},
         {"x", node.bounds.x}, {"y", node.bounds.y},
         {"width", node.bounds.width}, {"height", node.bounds.height},
         {"value", node.value}, {"minimum", node.minimum},
         {"maximum", node.maximum}, {"focused", node.focused},
         {"disabled", node.disabled}, {"checked", node.checked},
         {"focusable", node.focusable}});
  const std::string text = output.dump();
  return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_dev_jeapi_demi_android_DemiActivity_nativeAccessibilityAction(
    JNIEnv *environment, jclass, jint type, jstring nodeId, jfloat value,
    jstring text) {
  const char *idBytes = environment->GetStringUTFChars(nodeId, nullptr);
  const char *textBytes = text == nullptr
                              ? nullptr
                              : environment->GetStringUTFChars(text, nullptr);
  const auto boundedType = std::clamp(type, 0, 7);
  demi::runtime::ui::platformUiAccessibilityBridge().submitAction(
      {.type = static_cast<demi::runtime::ui::UiAccessibilityActionType>(boundedType),
       .nodeId = idBytes == nullptr ? std::string{} : std::string(idBytes),
       .value = value,
       .text = textBytes == nullptr ? std::string{} : std::string(textBytes)});
  if (idBytes != nullptr)
    environment->ReleaseStringUTFChars(nodeId, idBytes);
  if (textBytes != nullptr)
    environment->ReleaseStringUTFChars(text, textBytes);
}

namespace {
std::atomic_bool AndroidSurfaceAvailable = false;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_jeapi_demi_android_DemiActivity_nativeSurfaceAvailable(
    JNIEnv *, jclass, const jboolean available) {
  AndroidSurfaceAvailable.store(available == JNI_TRUE,
                                std::memory_order_release);
}

extern "C" bool DemiAndroidSurfaceAvailable(void) {
  return AndroidSurfaceAvailable.load(std::memory_order_acquire);
}

namespace {

constexpr const char *LogTag = "DemiEngine";
constexpr const char *ProjectDirectory = "project";
constexpr const char *ProjectFile = "project/demi.project.json";
constexpr const char *AssetIndexFile = "demi_asset_index.txt";
void logInfo(const std::string &message) {
  __android_log_print(ANDROID_LOG_INFO, LogTag, "%s", message.c_str());
}

void logError(const std::string &message) {
  __android_log_print(ANDROID_LOG_ERROR, LogTag, "%s", message.c_str());
}

bool copyAssetFile(AAssetManager *manager, const std::string &assetPath,
                   const std::filesystem::path &outputPath) {
  AAsset *asset =
      AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_STREAMING);
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

std::vector<std::string> readAssetIndex(AAssetManager *manager) {
  AAsset *asset =
      AAssetManager_open(manager, AssetIndexFile, AASSET_MODE_BUFFER);
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

void clearBundledProjectFiles(const std::filesystem::path &projectRoot) {
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
  error.clear();
  std::filesystem::remove_all(projectRoot / "packages", error);
}

std::filesystem::path prepareProject(AAssetManager *manager) {
  const std::filesystem::path storageRoot = SDL_GetAndroidInternalStoragePath();
  const std::filesystem::path projectRoot = storageRoot / ProjectDirectory;

  const std::vector<std::string> assetPaths = readAssetIndex(manager);
  if (assetPaths.empty()) {
    logError("Android asset index is empty.");
  }
  clearBundledProjectFiles(projectRoot);
  for (const std::string &assetPath : assetPaths) {
    if (!copyAssetFile(manager, assetPath, projectRoot / assetPath)) {
      logError("Failed to copy Android asset: " + assetPath);
    }
  }

  std::error_code error;
  std::filesystem::create_directories(projectRoot / "saves", error);

  return projectRoot / "demi.project.json";
}

} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  auto *environment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  const char *storagePath = SDL_GetAndroidInternalStoragePath();
  AAssetManager *assetManager = nullptr;
  if (environment != nullptr && activity != nullptr) {
    const jclass activityClass = environment->GetObjectClass(activity);
    const jmethodID getAssets = environment->GetMethodID(
        activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject javaAssets = getAssets == nullptr
                             ? nullptr
                             : environment->CallObjectMethod(activity, getAssets);
    if (javaAssets != nullptr) {
      assetManager = AAssetManager_fromJava(environment, javaAssets);
      environment->DeleteLocalRef(javaAssets);
    }
    environment->DeleteLocalRef(activityClass);
  }
  if (assetManager == nullptr || storagePath == nullptr) {
    logError("Android activity is unavailable.");
    return 1;
  }

  const std::filesystem::path projectPath = prepareProject(assetManager);
  const bool hotReload = std::filesystem::is_regular_file(
      std::filesystem::path(storagePath) / ".demi_hot_reload");
  logInfo("Launching " + std::string(ProjectFile));
  if (hotReload)
    logInfo("Physical-device hot reload enabled.");
  return demi::runtime::runProject(demi::runtime::RuntimeOptions{
      .projectPath = projectPath,
      .watch = hotReload,
  });
}
