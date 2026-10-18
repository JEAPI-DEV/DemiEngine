#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {
namespace {
Point3D point(Vec3 value) { return {.x = value.x, .y = value.y, .z = value.z}; }
} // namespace

OpaqueSceneCapture3D::Target *BgfxRenderer3D::prepareWaterCapture(
    const World &world, const BgfxCameraFrame3D &frame, std::string &error) {
  if (!frame.updateContent || resources_.shaderBackend() == "noop" ||
      frame.camera.debugMode != "shaded") {
    return nullptr;
  }
  const auto &waterSurfaces = entityQueries_.queryAny<
      terrain_detail::TerrainGeneratedWaterSurface>(world.entities);
  const bool hasWater =
      std::ranges::any_of(waterSurfaces, [&](std::size_t index) {
        const Entity &entity = world.entities[index];
        if (!entity.enabled) {
          return false;
        }
        const auto *mesh = entity.component<MeshRendererComponent>();
        if (!mesh ||
            (!frame.camera.renderMask.empty() && !mesh->renderLayer.empty() &&
             frame.camera.renderMask != mesh->renderLayer)) {
          return false;
        }
        const auto transform = resolveWorldTransform3D(world, entity);
        return transform && meshBoundsVisible3D(*mesh, *transform, frame);
      });
  if (!hasWater && !frame.underwater) {
    return nullptr;
  }
  auto width = frame.viewportWidth;
  auto height = frame.viewportHeight;
  if (const auto target = renderTargets_.find(frame.camera.renderTarget);
      target != renderTargets_.end()) {
    width = target->second.width;
    height = target->second.height;
  } else {
    const float scale = std::clamp(frame.camera.renderScale, 0.25F, 2.F);
    width = static_cast<std::uint16_t>(
        std::clamp(std::lround(width * scale), 1L, long(UINT16_MAX)));
    height = static_cast<std::uint16_t>(
        std::clamp(std::lround(height * scale), 1L, long(UINT16_MAX)));
  }
  if (!waterRenderer_.initialize(error)) {
    return nullptr;
  }
  return opaqueCapture_.prepare(frame.viewId, width, height, error);
}

bool BgfxRenderer3D::renderWaterAndTransparent(
    const SceneVisibility3D &visibility, BgfxCameraFrame3D &frame,
    const RenderTargetHandles &sourceTarget,
    OpaqueSceneCapture3D::Target &capture, const SceneLighting3D &sceneLighting,
    const std::array<DrawUniformValue, 8> &lightingUniforms,
    std::size_t firstTransparent, MeshSubmissionStatistics &meshStatistics,
    std::string &error) {
  const auto &lighting =
      frame.lightingOverride ? *frame.lightingOverride : sceneLighting;
  const bool directionalOnly = !lighting.hasAdditionalLights();
  const auto defaultMeshProgram =
      directionalOnly ? directionalMeshProgram_ : meshProgram_;
  const auto defaultInstancedProgram =
      directionalOnly ? directionalInstancedProgram_ : instancedMeshProgram_;
  const auto defaultSkinnedProgram =
      directionalOnly ? directionalSkinnedProgram_ : skinnedMeshProgram_;
  // A real target transition resolves the multisampled opaque colour before
  // the copy. A blit-only view can otherwise run while that target is still
  // active and read its previous frame's resolve image.
  const View2DConfig captureView{
      .id = static_cast<std::uint16_t>(frame.viewId + 1),
      .width = capture.width,
      .height = capture.height,
      .clear = false,
      .frameBuffer = capture.depth.frameBuffer,
      .debugName = "OpaqueColorCapture"};
  if (!primitives_.flush(error) ||
      !commands_.configureView2D(captureView, error) ||
      !commands_.copyTexture(frame.viewId + 1, sourceTarget.color,
                             capture.color, capture.width, capture.height,
                             error)) {
    return false;
  }
  frame.viewId += 2;
  const View3DConfig waterView{.id = frame.viewId,
                               .width = capture.width,
                               .height = capture.height,
                               .eye = point(frame.position),
                               .forward = point(frame.forward),
                               .up = point(frame.up),
                               .verticalFovDegrees = frame.camera.fov,
                               .nearClip = frame.camera.nearClip,
                               .farClip = frame.camera.farClip,
                               .orthographicSize =
                                   frame.camera.orthographicSize,
                               .perspective = frame.camera.perspective,
                               .clearColor = false,
                               .clearDepth = false,
                               .frameBuffer = sourceTarget.frameBuffer,
                               .sequential = true,
                               .debugName = "WaterAndTransparent"};
  if (!primitives_.begin(waterView, error) ||
      !lightBuffer_.prepare(frame.viewId, lighting, error)) {
    return false;
  }
  std::vector<VisibleMesh3D> waterSurfaces;
  SceneVisibility3D transparent;
  for (std::size_t index = firstTransparent; index < visibility.meshes.size();
       ++index) {
    const auto &visible = visibility.meshes[index];
    if (visible.entity
            ->component<terrain_detail::TerrainGeneratedWaterSurface>()) {
      waterSurfaces.push_back(visible);
    } else {
      transparent.meshes.push_back(visible);
    }
  }
  auto waterFrame = frame;
  waterFrame.viewportX = 0;
  waterFrame.viewportY = 0;
  waterFrame.viewportWidth = capture.width;
  waterFrame.viewportHeight = capture.height;
  const auto environment =
      sceneLighting.skyTexture.empty()
          ? TextureHandle{}
          : materialTexture(sceneLighting.skyTexture, ImageMipSemantic::Linear)
                .handle;
  const auto previousWaterStats = waterRenderer_.statistics();
  if (!waterRenderer_.draw(waterSurfaces, waterFrame,
                           capture.sample(resources_.limits().originBottomLeft),
                           environment, lighting, waterTime_, error)) {
    return false;
  }
  const auto &waterStats = waterRenderer_.statistics();
  meshStatistics.draws += waterStats.draws - previousWaterStats.draws;
  meshStatistics.triangles +=
      waterStats.triangles - previousWaterStats.triangles;
  if (!submitVisibleMeshes(transparent, frame, false, 0, lightingUniforms,
                           defaultMeshProgram, defaultInstancedProgram,
                           defaultSkinnedProgram, meshStatistics, error)) {
    return false;
  }
  return true;
}
} // namespace demi::runtime::render
