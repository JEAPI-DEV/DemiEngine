#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::platform {

enum class PermissionState {
  Unknown,
  NotRequested,
  Requesting,
  Granted,
  Denied,
  DeniedPermanently
};

struct PermissionEvent {
  std::string permission;
  PermissionState state = PermissionState::Unknown;
};

using PermissionResult =
    std::function<void(bool granted, bool deniedPermanently)>;
using PermissionRequester = std::function<bool(
    const std::string &permission, PermissionResult result,
    std::string &error)>;

class ApplicationPermissions {
public:
  ApplicationPermissions();
  ~ApplicationPermissions();

  void configure(std::vector<std::string> declaredPermissions);
  void setRequester(PermissionRequester requester);
  [[nodiscard]] PermissionState state(std::string_view permission) const;
  [[nodiscard]] bool request(std::string permission, std::string &error);
  [[nodiscard]] std::vector<PermissionEvent> takeEvents();

private:
  struct SharedState;
  std::shared_ptr<SharedState> state_;
  PermissionRequester requester_;
};

[[nodiscard]] const char *permissionStateName(PermissionState state);

} // namespace demi::runtime::platform
