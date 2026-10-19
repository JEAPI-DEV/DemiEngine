#include "demi/runtime/app/Bgfx3DAppHost.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>
#include <cstdint>

namespace demi::runtime {

void Bgfx3DAppHost::cancelScenePresentationPreparation() {
  preparingPresentationWorld_ = nullptr;
  presentationPreparedThisFrame_ = false;
  if (auto *renderer = rendererForPreparation()) {
    renderer->cancelScenePresentationPreparation();
  }
}

bool Bgfx3DAppHost::prepareScenePresentation(const World &world, bool &complete,
                                             std::string &error) {
  complete = false;
  auto *renderer = rendererForPreparation();
  if (renderer == nullptr) {
    error = "The bgfx 3D application host is not initialized.";
    return false;
  }
  // The Lua host clears this lease before activation/cancellation frees or
  // moves the prepared world. Re-touch demand while a ready scene waits.
  preparingPresentationWorld_ = &world;
  presentationPreparedThisFrame_ = true;
  const auto cameras = renderCameras3D(world);
  complete = true;
  const auto prepareCamera = [&](const Entity *entity) {
    render::BgfxCameraFrame3D frame;
    frame.camera = entity != nullptr ? *entity->component<Camera3DComponent>()
                                     : Camera3DComponent{};
    frame.cameraId = entity != nullptr ? entity->id : "fallback";
    frame.position = {};
    frame.forward = {0, 0, 1};
    frame.up = {0, 1, 0};
    frame.viewportWidth = static_cast<std::uint16_t>(std::clamp(
        frame.camera.viewportWidth * context_.viewportWidth(), 1.F, 65535.F));
    frame.viewportHeight = static_cast<std::uint16_t>(std::clamp(
        frame.camera.viewportHeight * context_.viewportHeight(), 1.F, 65535.F));
    if (entity != nullptr) {
      if (const auto transform = resolveWorldTransform3D(world, *entity)) {
        frame.position = transform->position;
        frame.forward =
            transformDirection3D(*transform, frame.camera.targetOffset);
        frame.up =
            transformDirection3D(*transform, {0, frame.camera.upAxis, 0});
      }
    }
    bool cameraReady = false;
    if (!renderer->prepareScenePresentation(world, frame, cameraReady, error)) {
      return false;
    }
    complete = complete && cameraReady;
    return true;
  };
  if (cameras.empty()) {
    return prepareCamera(nullptr);
  }
  for (const Entity *camera : cameras) {
    if (!prepareCamera(camera)) {
      return false;
    }
  }
  return true;
}

} // namespace demi::runtime
