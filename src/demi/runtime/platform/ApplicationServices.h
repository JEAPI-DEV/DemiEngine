#pragma once

#include "demi/runtime/platform/ApplicationPermissions.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::platform {

struct SafeAreaInsets {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
};

struct ApplicationLifecycleEvent {
  std::string type;
  unsigned generation = 0;
};

enum class Orientation { Unspecified, Portrait, Landscape };

class ApplicationServices {
public:
  void configureStorage(const std::string &applicationName,
                        const std::filesystem::path &projectDirectory);
  void updateDisplay(int width, int height, float logicalDpi,
                     SafeAreaInsets safeArea = {});
  void setFocused(bool focused);
  void setMinimized(bool minimized);
  void setSuspended(bool suspended);
  void notifyLowMemory();
  void notifyBackRequested();
  void setKeyboardVisible(bool visible);
  void requestOrientation(Orientation orientation);
  void setClipboardHandlers(std::function<std::string()> reader,
                            std::function<void(const std::string &)> writer);
  [[nodiscard]] std::string clipboard() const;
  void setClipboard(const std::string &text);
  void configurePermissions(std::vector<std::string> declaredPermissions);
  void setPermissionRequester(PermissionRequester requester);
  [[nodiscard]] PermissionState permissionState(
      std::string_view permission) const;
  [[nodiscard]] bool requestPermission(std::string permission,
                                       std::string &error);
  [[nodiscard]] std::vector<PermissionEvent> takePermissionEvents();
  [[nodiscard]] std::vector<ApplicationLifecycleEvent> takeLifecycleEvents();

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] float logicalDpi() const;
  [[nodiscard]] float uiScale() const;
  [[nodiscard]] SafeAreaInsets safeArea() const;
  [[nodiscard]] Orientation orientation() const;
  [[nodiscard]] Orientation requestedOrientation() const;
  [[nodiscard]] bool focused() const;
  [[nodiscard]] bool minimized() const;
  [[nodiscard]] bool suspended() const;
  [[nodiscard]] bool keyboardVisible() const;
  [[nodiscard]] unsigned lowMemoryGeneration() const;
  [[nodiscard]] const std::filesystem::path &userDataPath() const;
  [[nodiscard]] const std::filesystem::path &cachePath() const;

private:
  int width_ = 1;
  int height_ = 1;
  float logicalDpi_ = 96.0F;
  float uiScale_ = 1.0F;
  SafeAreaInsets safeArea_;
  Orientation orientation_ = Orientation::Unspecified;
  Orientation requestedOrientation_ = Orientation::Unspecified;
  bool focused_ = true;
  bool minimized_ = false;
  bool suspended_ = false;
  bool keyboardVisible_ = false;
  unsigned lowMemoryGeneration_ = 0;
  unsigned lifecycleGeneration_ = 0;
  std::vector<ApplicationLifecycleEvent> lifecycleEvents_;
  std::filesystem::path userDataPath_;
  std::filesystem::path cachePath_;
  mutable std::string clipboardFallback_;
  std::function<std::string()> clipboardReader_;
  std::function<void(const std::string &)> clipboardWriter_;
  ApplicationPermissions permissions_;
};

[[nodiscard]] const char *orientationName(Orientation orientation);

} // namespace demi::runtime::platform
