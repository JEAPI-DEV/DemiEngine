#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"
#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PostProcessStackComponent.h"

#include <cstdint>
#include <optional>
#include <string>

namespace demi::runtime::render {
inline constexpr std::uint16_t CameraViewCount3D = 5;

struct BgfxCameraFrame3D {
  std::string cameraId;
  Camera3DComponent camera;
  std::optional<PostProcessStackComponent> postProcess;
  DebugGeometry3DRequest debugGeometry;
  Vec3 position;
  Vec3 forward{0.0F, 0.0F, 1.0F};
  Vec3 up{0.0F, 1.0F, 0.0F};
  std::uint16_t viewportX = 0;
  std::uint16_t viewportY = 0;
  std::uint16_t viewportWidth = 0;
  std::uint16_t viewportHeight = 0;
  std::uint16_t viewId = 0;
  FrameBufferHandle frameBuffer;
  bool updateContent = true;
  std::optional<SceneLighting3D> lightingOverride;
  std::optional<Vec3> lodPosition;
  // Native host backbuffer/target sample mode; editor images default to 1.
  int destinationSamples = 1;
};

} // namespace demi::runtime::render
