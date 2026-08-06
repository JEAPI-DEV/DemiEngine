#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

class CameraRenderScheduler3D {
public:
  void beginFrame();
  [[nodiscard]] bool shouldRender(std::string_view cameraId,
                                  float updateInterval, float deltaTime);
  void endFrame();

private:
  struct CameraState {
    float elapsed = 0.0F;
  };

  std::unordered_map<std::string, CameraState> states_;
  std::unordered_set<std::string> activeCameraIds_;
};

} // namespace demi::runtime
