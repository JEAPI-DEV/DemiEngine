#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/Renderer3DBatcher.h"
#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>
#include <unordered_set>

namespace demi::runtime {
void Renderer3D::beginFrame(const Camera3DComponent &camera,
                            const Vec3 cameraPosition, const Vec3 cameraForward,
                            const Vec3 cameraUp, const int width,
                            const int height) {
  ProfileScope scope("Renderer3D.begin_frame");
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  cameraForward_ = cameraForward;
  cameraUp_ = cameraUp;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

  BeginDrawing();
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

  BeginMode3D(rlCamera);
}

void Renderer3D::drawWorld(World &world, const float deltaTime) {
  ProfileScope drawWorldScope("Renderer3D.draw_world");
  if (hasAlphaCutoutShader_) {
    const float viewPos[3] = {cameraPosition_.x, cameraPosition_.y,
                              cameraPosition_.z};
    const float fogColor[4] = {camera_.clearColor.r, camera_.clearColor.g,
                               camera_.clearColor.b, camera_.clearColor.a};
    constexpr float FogStart = 160.0F;
    constexpr float FogEnd = 224.0F;
    SetShaderValue(alphaCutoutShader_,
                   GetShaderLocation(alphaCutoutShader_, "viewPos"), viewPos,
                   SHADER_UNIFORM_VEC3);
    SetShaderValue(alphaCutoutShader_,
                   GetShaderLocation(alphaCutoutShader_, "fogColor"), fogColor,
                   SHADER_UNIFORM_VEC4);
    SetShaderValue(alphaCutoutShader_,
                   GetShaderLocation(alphaCutoutShader_, "fogStart"), &FogStart,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(alphaCutoutShader_,
                   GetShaderLocation(alphaCutoutShader_, "fogEnd"), &FogEnd,
                   SHADER_UNIFORM_FLOAT);
  }

  if (world.debug.drawOrder)
    for (const Entity &entity : world.entities) {
      if (entity.hasComponent<DirectionalLightComponent>()) {
        const ::Vector3 direction = renderer3d_detail::toRlVec3(
            entity.component<DirectionalLightComponent>()->direction);
        const ::Color color = renderer3d_detail::toRlColor(
            entity.component<DirectionalLightComponent>()->color);
        DrawLine3D({0, 0, 0}, direction, color);
      }
    }

  for (Entity &entity : world.entities) {
    if (entity.hasComponent<AnimationPlayer3DComponent>() &&
        entity.component<AnimationPlayer3DComponent>()->playing) {
      entity.component<AnimationPlayer3DComponent>()->time +=
          std::max(deltaTime, 0.0F) *
          entity.component<AnimationPlayer3DComponent>()->speed;
    }
  }

  std::vector<Entity *> visibleEntities;
  for (Entity &entity : world.entities) {
    if (entity.hasComponent<MeshRendererComponent>() ||
        entity.hasComponent<BoxCollider3DComponent>() ||
        entity.hasComponent<SphereCollider3DComponent>() ||
        entity.hasComponent<ModelCollider3DComponent>()) {
      if (entity.hasComponent<MeshRendererComponent>() && ![&]() {
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
    RuntimeProfiler::record("Renderer3D.material_batch", 0.0);
    for (Entity *entity : batch.entities)
      renderer3d_detail::drawMeshEntity(
          world, *entity, textures_, models_, modelPaths_, modelTextures_,
          modelTextureSettings_, modelAnimations_, animatedModels_,
          dynamicModels_, world.debug.colliders,
          hasAlphaCutoutShader_ ? &alphaCutoutShader_ : nullptr);
  }

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
  EndDrawing();
}
} // namespace demi::runtime
