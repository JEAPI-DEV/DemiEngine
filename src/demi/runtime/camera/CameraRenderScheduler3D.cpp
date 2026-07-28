#include "demi/runtime/camera/CameraRenderScheduler3D.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

void CameraRenderScheduler3D::beginFrame() { activeCameraIds_.clear(); }

bool CameraRenderScheduler3D::shouldRender(const std::string_view cameraId,
                                           const float updateInterval,
                                           const float deltaTime) {
  const std::string id(cameraId);
  activeCameraIds_.insert(id);
  if (updateInterval <= 0.0F) {
    states_.erase(id);
    return true;
  }

  const auto [iterator, inserted] = states_.try_emplace(id);
  if (inserted)
    return true;

  CameraState &state = iterator->second;
  state.elapsed += std::max(deltaTime, 0.0F);
  if (state.elapsed < updateInterval)
    return false;

  state.elapsed = std::fmod(state.elapsed, updateInterval);
  return true;
}

void CameraRenderScheduler3D::endFrame() {
  std::erase_if(states_, [&](const auto &entry) {
    return !activeCameraIds_.contains(entry.first);
  });
}

} // namespace demi::runtime
