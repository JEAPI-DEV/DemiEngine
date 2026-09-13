#include "demi/runtime/platform/ApplicationPermissions.h"
#include "demi/runtime/platform/ApplicationServices.h"

#include <cassert>
#include <string>

int main() {
  using namespace demi::runtime::platform;

  ApplicationPermissions permissions;
  permissions.configure({"android.permission.CAMERA",
                         "android.permission.RECORD_AUDIO"});
  assert(permissions.state("android.permission.CAMERA") ==
         PermissionState::NotRequested);
  assert(permissions.state("android.permission.INTERNET") ==
         PermissionState::Unknown);

  PermissionResult pending;
  int requests = 0;
  permissions.setRequester(
      [&](const std::string &permission, PermissionResult result,
          std::string &) {
        assert(permission == "android.permission.CAMERA");
        ++requests;
        pending = std::move(result);
        return true;
      });

  std::string error;
  assert(!permissions.request("android.permission.INTERNET", error));
  assert(error.find("not declared") != std::string::npos);
  error.clear();
  assert(permissions.request("android.permission.CAMERA", error));
  assert(permissions.state("android.permission.CAMERA") ==
         PermissionState::Requesting);
  assert(permissions.request("android.permission.CAMERA", error));
  assert(requests == 1);

  pending(false, true);
  assert(permissions.state("android.permission.CAMERA") ==
         PermissionState::DeniedPermanently);
  auto events = permissions.takeEvents();
  assert(events.size() == 1);
  assert(events.front().permission == "android.permission.CAMERA");
  assert(events.front().state == PermissionState::DeniedPermanently);
  assert(permissions.takeEvents().empty());

  PermissionResult staleResult;
  permissions.setRequester(
      [&](const std::string &, PermissionResult result, std::string &) {
        staleResult = std::move(result);
        return true;
      });
  permissions.configure({"android.permission.CAMERA"});
  assert(permissions.request("android.permission.CAMERA", error));
  permissions.configure({"android.permission.CAMERA"});
  staleResult(true, false);
  assert(permissions.state("android.permission.CAMERA") ==
         PermissionState::NotRequested);

  permissions.configure({"android.permission.CAMERA",
                         "android.permission.RECORD_AUDIO"});
  permissions.setRequester({});
  assert(permissions.request("android.permission.RECORD_AUDIO", error));
  assert(permissions.state("android.permission.RECORD_AUDIO") ==
         PermissionState::Granted);

  PermissionResult lateResult;
  {
    ApplicationPermissions transient;
    transient.configure({"android.permission.CAMERA"});
    transient.setRequester(
        [&](const std::string &, PermissionResult result, std::string &) {
          lateResult = std::move(result);
          return true;
        });
    assert(transient.request("android.permission.CAMERA", error));
  }
  lateResult(true, false);

  ApplicationServices application;
  application.setFocused(false);
  application.setMinimized(true);
  application.setSuspended(true);
  application.notifyLowMemory();
  application.notifyBackRequested();
  application.updateDisplay(
      1280, 720, 144.0F,
      {.left = 4.0F, .top = 8.0F, .right = 4.0F, .bottom = 0.0F});
  const auto lifecycle = application.takeLifecycleEvents();
  assert(lifecycle.size() == 7);
  assert(lifecycle[0].type == "focus_lost");
  assert(lifecycle[1].type == "minimized");
  assert(lifecycle[2].type == "suspended");
  assert(lifecycle[3].type == "low_memory");
  assert(lifecycle[4].type == "back_requested");
  assert(lifecycle[5].type == "display_changed");
  assert(lifecycle[6].type == "safe_area_changed");
  assert(application.takeLifecycleEvents().empty());
  return 0;
}
