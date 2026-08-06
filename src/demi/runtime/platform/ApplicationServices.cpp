#include "demi/runtime/platform/ApplicationServices.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/configuration.h>
#include <android/native_activity.h>
#include <jni.h>
extern "C" ANativeActivity *DemiGetNativeActivity(void);
#endif

namespace demi::runtime::platform {
namespace {

std::string safeName(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return std::isalnum(c) != 0 ? static_cast<char>(std::tolower(c)) : '_';
  });
  return value.empty() ? "demi_game" : value;
}

#if !defined(__ANDROID__)
std::filesystem::path environmentPath(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr ? std::filesystem::path{} : value;
}
#endif

#if defined(__ANDROID__)
SafeAreaInsets androidSafeArea() {
  ANativeActivity *activity = DemiGetNativeActivity();
  if (activity == nullptr || activity->vm == nullptr ||
      activity->clazz == nullptr)
    return {};
  JNIEnv *environment = nullptr;
  bool attached = false;
  if (activity->vm->GetEnv(reinterpret_cast<void **>(&environment),
                           JNI_VERSION_1_6) != JNI_OK) {
    if (activity->vm->AttachCurrentThread(&environment, nullptr) != JNI_OK)
      return {};
    attached = true;
  }
  SafeAreaInsets result;
  const jclass activityClass = environment->GetObjectClass(activity->clazz);
  const jmethodID getWindow =
      environment->GetMethodID(activityClass, "getWindow",
                               "()Landroid/view/Window;");
  jobject window =
      getWindow == nullptr
          ? nullptr
          : environment->CallObjectMethod(activity->clazz, getWindow);
  if (window != nullptr) {
    const jclass windowClass = environment->GetObjectClass(window);
    const jmethodID getDecorView = environment->GetMethodID(
        windowClass, "getDecorView", "()Landroid/view/View;");
    jobject view = getDecorView == nullptr
                       ? nullptr
                       : environment->CallObjectMethod(window, getDecorView);
    if (view != nullptr) {
      const jclass viewClass = environment->GetObjectClass(view);
      const jmethodID getInsets = environment->GetMethodID(
          viewClass, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
      jobject insets =
          getInsets == nullptr
              ? nullptr
              : environment->CallObjectMethod(view, getInsets);
      if (insets != nullptr) {
        const jclass insetsClass = environment->GetObjectClass(insets);
        const auto inset = [&](const char *method) {
          const jmethodID id =
              environment->GetMethodID(insetsClass, method, "()I");
          return id == nullptr
                     ? 0.0F
                     : static_cast<float>(
                           environment->CallIntMethod(insets, id));
        };
        result = {.left = inset("getSystemWindowInsetLeft"),
                  .top = inset("getSystemWindowInsetTop"),
                  .right = inset("getSystemWindowInsetRight"),
                  .bottom = inset("getSystemWindowInsetBottom")};
        environment->DeleteLocalRef(insetsClass);
        environment->DeleteLocalRef(insets);
      }
      environment->DeleteLocalRef(viewClass);
      environment->DeleteLocalRef(view);
    }
    environment->DeleteLocalRef(windowClass);
    environment->DeleteLocalRef(window);
  }
  environment->DeleteLocalRef(activityClass);
  if (attached)
    activity->vm->DetachCurrentThread();
  return result;
}

float androidLogicalDpi() {
  ANativeActivity *activity = DemiGetNativeActivity();
  if (activity == nullptr || activity->assetManager == nullptr)
    return 0.0F;
  AConfiguration *configuration = AConfiguration_new();
  AConfiguration_fromAssetManager(configuration, activity->assetManager);
  const int density = AConfiguration_getDensity(configuration);
  AConfiguration_delete(configuration);
  return density > 0 && density != ACONFIGURATION_DENSITY_NONE
             ? static_cast<float>(density)
             : 0.0F;
}
#endif

} // namespace

void ApplicationServices::configureStorage(
    const std::string &applicationName,
    const std::filesystem::path &projectDirectory) {
  const std::string folder = safeName(applicationName);
#if defined(__ANDROID__)
  if (ANativeActivity *activity = DemiGetNativeActivity();
      activity != nullptr && activity->internalDataPath != nullptr) {
    userDataPath_ = std::filesystem::path(activity->internalDataPath) / folder;
    cachePath_ =
        activity->externalDataPath != nullptr
            ? std::filesystem::path(activity->externalDataPath) / "cache"
            : userDataPath_ / "cache";
  }
#else
  std::filesystem::path dataRoot = environmentPath("XDG_DATA_HOME");
  std::filesystem::path cacheRoot = environmentPath("XDG_CACHE_HOME");
  if (dataRoot.empty()) {
    const std::filesystem::path home = environmentPath("HOME");
    dataRoot = home.empty() ? projectDirectory / ".data"
                            : home / ".local" / "share";
  }
  if (cacheRoot.empty()) {
    const std::filesystem::path home = environmentPath("HOME");
    cacheRoot =
        home.empty() ? projectDirectory / ".cache" : home / ".cache";
  }
  userDataPath_ = dataRoot / folder;
  cachePath_ = cacheRoot / folder;
#endif
  if (userDataPath_.empty())
    userDataPath_ = projectDirectory / ".data";
  if (cachePath_.empty())
    cachePath_ = projectDirectory / ".cache";
  std::error_code ignored;
  std::filesystem::create_directories(userDataPath_, ignored);
  std::filesystem::create_directories(cachePath_, ignored);
}

void ApplicationServices::updateDisplay(const int width, const int height,
                                        const float logicalDpi,
                                        SafeAreaInsets safeArea) {
#if defined(__ANDROID__)
  if (safeArea.left == 0.0F && safeArea.top == 0.0F &&
      safeArea.right == 0.0F && safeArea.bottom == 0.0F)
    safeArea = androidSafeArea();
  const float platformDpi = androidLogicalDpi();
#else
  const float platformDpi = 0.0F;
#endif
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);
  logicalDpi_ =
      std::max(platformDpi > 0.0F ? platformDpi : logicalDpi, 1.0F);
  uiScale_ = std::clamp(logicalDpi_ / 96.0F, 0.5F, 4.0F);
  safeArea_ = safeArea;
  orientation_ = width_ >= height_ ? Orientation::Landscape
                                    : Orientation::Portrait;
}

void ApplicationServices::setFocused(const bool focused) { focused_ = focused; }
void ApplicationServices::setMinimized(const bool minimized) {
  minimized_ = minimized;
}
void ApplicationServices::setSuspended(const bool suspended) {
  suspended_ = suspended;
}
void ApplicationServices::notifyLowMemory() { ++lowMemoryGeneration_; }
void ApplicationServices::setKeyboardVisible(const bool visible) {
  keyboardVisible_ = visible;
}
void ApplicationServices::requestOrientation(const Orientation orientation) {
  requestedOrientation_ = orientation;
#if defined(__ANDROID__)
  ANativeActivity *activity = DemiGetNativeActivity();
  if (activity == nullptr || activity->vm == nullptr ||
      activity->clazz == nullptr)
    return;
  JNIEnv *environment = nullptr;
  bool attached = false;
  if (activity->vm->GetEnv(reinterpret_cast<void **>(&environment),
                           JNI_VERSION_1_6) != JNI_OK) {
    if (activity->vm->AttachCurrentThread(&environment, nullptr) != JNI_OK)
      return;
    attached = true;
  }
  const jclass activityClass = environment->GetObjectClass(activity->clazz);
  const jmethodID method = environment->GetMethodID(
      activityClass, "setRequestedOrientation", "(I)V");
  if (method != nullptr) {
    const jint requested = orientation == Orientation::Portrait
                               ? 1
                               : orientation == Orientation::Landscape ? 0
                                                                       : -1;
    environment->CallVoidMethod(activity->clazz, method, requested);
  }
  environment->DeleteLocalRef(activityClass);
  if (attached)
    activity->vm->DetachCurrentThread();
#endif
}
void ApplicationServices::setClipboardHandlers(
    std::function<std::string()> reader,
    std::function<void(const std::string &)> writer) {
  clipboardReader_ = std::move(reader);
  clipboardWriter_ = std::move(writer);
}
std::string ApplicationServices::clipboard() const {
  return clipboardReader_ ? clipboardReader_() : clipboardFallback_;
}
void ApplicationServices::setClipboard(const std::string &text) {
  clipboardFallback_ = text;
  if (clipboardWriter_)
    clipboardWriter_(text);
}
int ApplicationServices::width() const { return width_; }
int ApplicationServices::height() const { return height_; }
float ApplicationServices::logicalDpi() const { return logicalDpi_; }
float ApplicationServices::uiScale() const { return uiScale_; }
SafeAreaInsets ApplicationServices::safeArea() const { return safeArea_; }
Orientation ApplicationServices::orientation() const { return orientation_; }
Orientation ApplicationServices::requestedOrientation() const {
  return requestedOrientation_;
}
bool ApplicationServices::focused() const { return focused_; }
bool ApplicationServices::minimized() const { return minimized_; }
bool ApplicationServices::suspended() const { return suspended_; }
bool ApplicationServices::keyboardVisible() const { return keyboardVisible_; }
unsigned ApplicationServices::lowMemoryGeneration() const {
  return lowMemoryGeneration_;
}
const std::filesystem::path &ApplicationServices::userDataPath() const {
  return userDataPath_;
}
const std::filesystem::path &ApplicationServices::cachePath() const {
  return cachePath_;
}

const char *orientationName(const Orientation orientation) {
  if (orientation == Orientation::Portrait)
    return "portrait";
  if (orientation == Orientation::Landscape)
    return "landscape";
  return "unspecified";
}

} // namespace demi::runtime::platform
