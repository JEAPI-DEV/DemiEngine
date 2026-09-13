#include "demi/runtime/platform/ApplicationPermissions.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

namespace demi::runtime::platform {

struct ApplicationPermissions::SharedState {
  mutable std::mutex mutex;
  std::map<std::string, PermissionState, std::less<>> states;
  std::vector<PermissionEvent> events;
  unsigned generation = 0;
};

ApplicationPermissions::ApplicationPermissions()
    : state_(std::make_shared<SharedState>()) {}

ApplicationPermissions::~ApplicationPermissions() = default;

void ApplicationPermissions::configure(
    std::vector<std::string> declaredPermissions) {
  std::ranges::sort(declaredPermissions);
  declaredPermissions.erase(
      std::unique(declaredPermissions.begin(), declaredPermissions.end()),
      declaredPermissions.end());
  std::scoped_lock lock(state_->mutex);
  ++state_->generation;
  state_->states.clear();
  state_->events.clear();
  for (const std::string &permission : declaredPermissions)
    state_->states.emplace(permission, PermissionState::NotRequested);
}

void ApplicationPermissions::setRequester(PermissionRequester requester) {
  requester_ = std::move(requester);
}

PermissionState
ApplicationPermissions::state(const std::string_view permission) const {
  std::scoped_lock lock(state_->mutex);
  const auto found = state_->states.find(permission);
  return found == state_->states.end() ? PermissionState::Unknown
                                       : found->second;
}

bool ApplicationPermissions::request(std::string permission,
                                     std::string &error) {
  {
    std::scoped_lock lock(state_->mutex);
    const auto found = state_->states.find(permission);
    if (found == state_->states.end()) {
      error = "Permission is not declared by project build settings: " +
              permission;
      return false;
    }
    if (found->second == PermissionState::Requesting ||
        found->second == PermissionState::Granted)
      return true;
    found->second = PermissionState::Requesting;
  }

  const std::weak_ptr<SharedState> weakState = state_;
  unsigned requestGeneration = 0;
  {
    std::scoped_lock lock(state_->mutex);
    requestGeneration = state_->generation;
  }
  const auto result = [weakState, permission, requestGeneration](
                          const bool granted, const bool permanentlyDenied) {
    const auto shared = weakState.lock();
    if (!shared)
      return;
    const PermissionState next =
        granted ? PermissionState::Granted
                : permanentlyDenied ? PermissionState::DeniedPermanently
                                    : PermissionState::Denied;
    std::scoped_lock lock(shared->mutex);
    if (shared->generation != requestGeneration)
      return;
    const auto found = shared->states.find(permission);
    if (found == shared->states.end())
      return;
    found->second = next;
    shared->events.push_back({.permission = permission, .state = next});
  };

  if (!requester_) {
    result(true, false);
    return true;
  }
  if (requester_(permission, result, error))
    return true;

  std::scoped_lock lock(state_->mutex);
  if (const auto found = state_->states.find(permission);
      found != state_->states.end())
    found->second = PermissionState::NotRequested;
  return false;
}

std::vector<PermissionEvent> ApplicationPermissions::takeEvents() {
  std::scoped_lock lock(state_->mutex);
  return std::exchange(state_->events, {});
}

const char *permissionStateName(const PermissionState state) {
  switch (state) {
  case PermissionState::Unknown:
    return "unknown";
  case PermissionState::NotRequested:
    return "not_requested";
  case PermissionState::Requesting:
    return "requesting";
  case PermissionState::Granted:
    return "granted";
  case PermissionState::Denied:
    return "denied";
  case PermissionState::DeniedPermanently:
    return "denied_permanently";
  }
  return "unknown";
}

} // namespace demi::runtime::platform
