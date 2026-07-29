#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/Lighting3D.h"
#include "demi/runtime/render/Renderer3DBatcher.h"
#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/render/WorldText3DRenderer.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace demi::runtime {
void Renderer3D::beginFrame(const int width, const int height,
                            const Color clearColor) {
  ProfileScope scope("Renderer3D.begin_frame");
  frameWidth_ = std::max(width, 1);
  frameHeight_ = std::max(height, 1);
  statistics_.reset();
  particlesUpdated_ = false;
  usedCameraSurfaces_.clear();
  BeginDrawing();
  ClearBackground(renderer3d_detail::toRlColor(clearColor));
}

void Renderer3D::beginCamera(const std::string &cameraId,
                            const Camera3DComponent &camera,
                            const Vec3 cameraPosition,
                            const Vec3 cameraForward, const Vec3 cameraUp) {
  ProfileScope scope("Renderer3D.begin_camera");
  configureCameraOutput(cameraId, camera);
  cameraPosition_ = cameraPosition;
  cameraForward_ = cameraForward;
  cameraUp_ = cameraUp;

  const std::string &surfaceId = cameraSurfaceTextureIds_.at(cameraId);
  const auto surface = cameraSurfaces_.find(surfaceId);
  BeginTextureMode(surface->second);
  if (camera.clearMode != "none")
    ClearBackground(renderer3d_detail::toRlColor(camera.clearColor));

  const ::Vector3 position = renderer3d_detail::toRlVec3(cameraPosition);
  const ::Vector3 target = {
      position.x + cameraForward.x,
      position.y + cameraForward.y,
      position.z + cameraForward.z,
  };
  const ::Vector3 up = renderer3d_detail::toRlVec3(cameraUp);
  ::Camera3D rlCamera{
      .position = position,
      .target = target,
      .up = up,
      .fovy = camera.perspective ? std::max(camera.fov, 1.0F)
                                 : std::max(camera.orthographicSize, 0.01F),
      .projection =
          camera.perspective ? CAMERA_PERSPECTIVE : CAMERA_ORTHOGRAPHIC,
  };

  raylibCamera_ = rlCamera;
  BeginMode3D(raylibCamera_);
}

void Renderer3D::configureCameraOutput(const std::string &cameraId,
                                       const Camera3DComponent &camera) {
  camera_ = camera;
  currentCameraId_ = cameraId;
  const int destinationWidth = std::max(
      static_cast<int>(std::round(camera.viewportWidth * frameWidth_)), 1);
  const int destinationHeight = std::max(
      static_cast<int>(std::round(camera.viewportHeight * frameHeight_)), 1);
  cameraDestination_ = Rectangle{
      std::round(camera.viewportX * frameWidth_),
      std::round(camera.viewportY * frameHeight_),
      static_cast<float>(destinationWidth),
      static_cast<float>(destinationHeight)};
  width_ = std::max(
      static_cast<int>(std::round(destinationWidth * camera.renderScale)), 1);
  height_ = std::max(
      static_cast<int>(std::round(destinationHeight * camera.renderScale)), 1);
  if (!camera.renderTarget.empty())
    if (const auto target = renderTargets_.find(camera.renderTarget);
        target != renderTargets_.end()) {
      width_ = target->second.width;
      height_ = target->second.height;
    }

  const std::string surfaceId =
      camera.renderTarget.empty() ? "camera://" + cameraId
                                  : camera.renderTarget;
  usedCameraSurfaces_.insert(surfaceId);
  auto surface = cameraSurfaces_.find(surfaceId);
  if (surface != cameraSurfaces_.end() &&
      (surface->second.texture.width != width_ ||
       surface->second.texture.height != height_)) {
    UnloadRenderTexture(surface->second);
    surface = cameraSurfaces_.erase(surface);
  }
  if (surface == cameraSurfaces_.end())
    surface =
        cameraSurfaces_.emplace(surfaceId, LoadRenderTexture(width_, height_))
            .first;
  cameraSurfaceTextureIds_[cameraId] = surfaceId;
  if (!camera.renderTarget.empty())
    textures_[camera.renderTarget] = surface->second.texture;
  statistics_.renderTargetBytes +=
      static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 8U;
}

void Renderer3D::drawWorld(World &world, const float deltaTime) {
  ProfileScope drawWorldScope("Renderer3D.draw_world");
  const LightingFrame3D lighting =
      collectLighting3D(world, camera_.renderMask, statistics_);
  if (hasAlphaCutoutShader_)
    applyLighting3D(lighting, alphaCutoutShader_, cameraPosition_);

  if (world.debug.drawOrder)
    for (const Entity &entity : world.entities) {
      if (!entity.enabled)
        continue;
      if (entity.hasComponent<DirectionalLightComponent>()) {
        const ::Vector3 direction = renderer3d_detail::toRlVec3(
            entity.component<DirectionalLightComponent>()->direction);
        const ::Color color = renderer3d_detail::toRlColor(
            entity.component<DirectionalLightComponent>()->color);
        DrawLine3D({0, 0, 0}, direction, color);
      }
    }

  (void)deltaTime;

  std::vector<Entity *> visibleEntities;
  for (Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (entity.hasComponent<MeshRendererComponent>() ||
        entity.hasComponent<BoxCollider3DComponent>() ||
        entity.hasComponent<SphereCollider3DComponent>() ||
        entity.hasComponent<CapsuleCollider3DComponent>() ||
        entity.hasComponent<ConvexCollider3DComponent>() ||
        entity.hasComponent<ModelCollider3DComponent>()) {
      if (entity.hasComponent<MeshRendererComponent>() && ![&]() {
            const auto *mesh = entity.component<MeshRendererComponent>();
            if (!camera_.renderMask.empty() && !mesh->renderLayer.empty() &&
                camera_.renderMask != mesh->renderLayer)
              return false;
            ProfileScope scope("Renderer3D.frustum_cull");
            return renderer3d_detail::meshEntityVisible(
                world, entity, *entity.component<MeshRendererComponent>(),
                cameraPosition_, cameraForward_, cameraUp_, camera_,
                static_cast<float>(width_) / static_cast<float>(height_));
          }()) {
        continue;
      }
      visibleEntities.push_back(&entity);
    }
  }
  std::vector<RenderBatch3D> renderBatches;
  {
    ProfileScope scope("Renderer3D.build_batches");
    renderBatches = buildRenderBatches3D(visibleEntities);
  }
  for (RenderBatch3D &batch : renderBatches) {
    ++statistics_.batches;
    RuntimeProfiler::record("Renderer3D.material_batch", 0.0);
    for (Entity *entity : batch.entities) {
      const auto *mesh = entity->component<MeshRendererComponent>();
      if (mesh != nullptr) {
        if (!mesh->vertices.empty())
          statistics_.triangles += mesh->vertices.size() / 3U;
        else if (const auto model = models_.find(mesh->model);
                 model != models_.end())
          for (int index = 0; index < model->second.meshCount; ++index)
            statistics_.triangles += static_cast<std::size_t>(
                std::max(model->second.meshes[index].triangleCount, 0));
        else if (mesh->shape == "plane")
          statistics_.triangles += 2U;
        else if (mesh->shape == "sphere")
          statistics_.triangles += 1024U;
        else
          statistics_.triangles += 12U;
      }
      renderer3d_detail::drawMeshEntity(
          world, *entity, textures_, models_, modelPaths_, modelTextures_,
          modelTextureSettings_, modelAnimations_, animatedModels_,
          dynamicModels_, materials_, shaders_, world.debug.colliders,
          hasAlphaCutoutShader_ ? &alphaCutoutShader_ : nullptr);
    }
  }
  if (!particlesUpdated_)
    particleSystem_.update(world, deltaTime);
  particleSystem_.draw(world, raylibCamera_, textures_, materials_,
                       camera_.renderMask, statistics_);
  particlesUpdated_ = true;
  drawWorldText3D(world, raylibCamera_, cameraPosition_, camera_.renderMask,
                  statistics_);

  std::unordered_set<std::string> liveDynamicModelIds;
  for (const Entity &entity : world.entities) {
    if (entity.hasComponent<MeshRendererComponent>() &&
        !entity.component<MeshRendererComponent>()->vertices.empty()) {
      liveDynamicModelIds.insert(entity.id);
    }
  }
  for (auto iterator = dynamicModels_.begin();
       iterator != dynamicModels_.end();) {
    if (liveDynamicModelIds.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    if (iterator->second.hasModel) {
      UnloadModel(iterator->second.model);
    }
    iterator = dynamicModels_.erase(iterator);
  }

  if (!world.debug.grid)
    return;
  constexpr int slices = 40;
  constexpr float spacing = 1.0F;
  constexpr float half = static_cast<float>(slices) * spacing * 0.5F;
  constexpr float y = 0.0125F;
  const ::Color gridColor = {170, 188, 180, 110};
  for (int i = 0; i <= slices; ++i) {
    const float p = -half + static_cast<float>(i) * spacing;
    DrawLine3D({-half, y, p}, {half, y, p}, gridColor);
    DrawLine3D({p, y, -half}, {p, y, half}, gridColor);
  }
}

void Renderer3D::endFrame() {
  ProfileScope scope("Renderer3D.end_frame");
  RuntimeProfiler::setGauge("Renderer3D.stats.batches",
                            static_cast<double>(statistics_.batches));
  RuntimeProfiler::setGauge("Renderer3D.stats.triangles",
                            static_cast<double>(statistics_.triangles));
  RuntimeProfiler::setGauge("Renderer3D.stats.particles",
                            static_cast<double>(statistics_.particles));
  RuntimeProfiler::setGauge("Renderer3D.stats.lights",
                            static_cast<double>(statistics_.lights));
  RuntimeProfiler::setGauge("Renderer3D.stats.shadow_passes",
                            static_cast<double>(statistics_.shadowPasses));
  RuntimeProfiler::setGauge(
      "Renderer3D.stats.render_target_bytes",
      static_cast<double>(statistics_.renderTargetBytes));
  std::erase_if(cameraSurfaceTextureIds_, [&](const auto &entry) {
    return !usedCameraSurfaces_.contains(entry.second);
  });
  for (auto iterator = cameraSurfaces_.begin();
       iterator != cameraSurfaces_.end();) {
    if (usedCameraSurfaces_.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    if (iterator->first.starts_with("asset://"))
      textures_.erase(iterator->first);
    UnloadRenderTexture(iterator->second);
    iterator = cameraSurfaces_.erase(iterator);
  }
  EndDrawing();
}

void Renderer3D::endCamera(const PostProcessStackComponent *postProcess,
                           const World *hudTarget) {
  ProfileScope scope("Renderer3D.end_camera");
  EndMode3D();
  if (hudTarget != nullptr) {
    const int savedWidth = frameWidth_;
    const int savedHeight = frameHeight_;
    frameWidth_ = width_;
    frameHeight_ = height_;
    drawHud(*hudTarget);
    frameWidth_ = savedWidth;
    frameHeight_ = savedHeight;
  }
  EndTextureMode();
  presentCurrentCamera(postProcess);
}

void Renderer3D::presentCamera(
    const std::string &cameraId, const Camera3DComponent &camera,
    const PostProcessStackComponent *postProcess) {
  ProfileScope scope("Renderer3D.present_cached_camera");
  configureCameraOutput(cameraId, camera);
  presentCurrentCamera(postProcess);
}

bool Renderer3D::canPresentCamera(const std::string &cameraId,
                                  const Camera3DComponent &camera) const {
  const auto textureId = cameraSurfaceTextureIds_.find(cameraId);
  if (textureId == cameraSurfaceTextureIds_.end())
    return false;
  const auto surface = cameraSurfaces_.find(textureId->second);
  if (surface == cameraSurfaces_.end())
    return false;

  int expectedWidth = std::max(
      static_cast<int>(
          std::round(camera.viewportWidth * frameWidth_ * camera.renderScale)),
      1);
  int expectedHeight = std::max(
      static_cast<int>(std::round(camera.viewportHeight * frameHeight_ *
                                  camera.renderScale)),
      1);
  if (!camera.renderTarget.empty())
    if (const auto target = renderTargets_.find(camera.renderTarget);
        target != renderTargets_.end()) {
      expectedWidth = target->second.width;
      expectedHeight = target->second.height;
    }
  return surface->second.texture.width == expectedWidth &&
         surface->second.texture.height == expectedHeight;
}

void Renderer3D::presentCurrentCamera(
    const PostProcessStackComponent *postProcess) {
  const auto surfaceId = cameraSurfaceTextureIds_.find(currentCameraId_);
  if (surfaceId == cameraSurfaceTextureIds_.end())
    return;
  const std::string &id = surfaceId->second;
  const auto surface = cameraSurfaces_.find(id);
  if (surface == cameraSurfaces_.end())
    return;

  if (postProcess != nullptr && hasPostProcessShader_) {
    const float resolution[]{static_cast<float>(width_),
                             static_cast<float>(height_)};
    const float tint[]{postProcess->tint.r, postProcess->tint.g,
                       postProcess->tint.b, postProcess->tint.a};
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "resolution"),
                   resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "exposure"),
                   &postProcess->exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "contrast"),
                   &postProcess->contrast, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "saturation"),
                   &postProcess->saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "vignette"),
                   &postProcess->vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "bloom"),
                   &postProcess->bloom, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "bloomThreshold"),
                   &postProcess->bloomThreshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postProcessShader_,
                   GetShaderLocation(postProcessShader_, "tint"), tint,
                   SHADER_UNIFORM_VEC4);
    BeginShaderMode(postProcessShader_);
  }
  DrawTexturePro(surface->second.texture,
                 {0.0F, 0.0F, static_cast<float>(width_),
                  -static_cast<float>(height_)},
                 cameraDestination_, {}, 0.0F, ::Color{255, 255, 255, 255});
  if (postProcess != nullptr && hasPostProcessShader_)
    EndShaderMode();
  if (postProcess != nullptr && postProcess->fade > 0.0F) {
    Color fade = postProcess->fadeColor;
    fade.a *= postProcess->fade;
    DrawRectangleRec(cameraDestination_, renderer3d_detail::toRlColor(fade));
  }
}
} // namespace demi::runtime
