#include "editor/EditorViewportOverlay2D.h"

#include "editor/EditorViewportProjection2D.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::editor {
namespace {

void addRectangle(std::vector<EditorOverlayLine2D> &lines,
                  const runtime::World &world, const runtime::Entity &entity,
                  const EditorSceneView2DCamera &camera,
                  const runtime::Vec2 viewport, const runtime::Vec2 minimum,
                  const runtime::Vec2 maximum, const std::uint32_t color) {
  const std::array corners{
      runtime::worldPoint2D(world, entity, minimum),
      runtime::worldPoint2D(world, entity, {maximum.x, minimum.y}),
      runtime::worldPoint2D(world, entity, maximum),
      runtime::worldPoint2D(world, entity, {minimum.x, maximum.y})};
  for (std::size_t index = 0; index < corners.size(); ++index)
    lines.push_back(
        {.start = projectScenePoint2D(camera, corners[index], viewport),
         .end = projectScenePoint2D(
             camera, corners[(index + 1) % corners.size()], viewport),
         .rgba = color,
         .width = 1.5F});
}

void addGrid(std::vector<EditorOverlayLine2D> &lines,
             const EditorSceneView2DCamera &camera,
             const runtime::Vec2 viewport) {
  const float ppu = std::max(viewport.y, 1.0F) /
                    std::max(camera.projection.orthographicSize * 2.0F, 0.01F);
  float spacing = 1.0F;
  while (spacing * ppu < 20.0F)
    spacing *= 10.0F;
  const runtime::Vec2 minimum =
      unprojectScenePoint2D(camera, {0.0F, viewport.y}, viewport);
  const runtime::Vec2 maximum =
      unprojectScenePoint2D(camera, {viewport.x, 0.0F}, viewport);
  const float firstX = std::floor(minimum.x / spacing) * spacing;
  const float firstY = std::floor(minimum.y / spacing) * spacing;
  for (float x = firstX; x <= maximum.x; x += spacing) {
    const float screen = projectScenePoint2D(camera, {x, 0.0F}, viewport).x;
    lines.push_back(
        {.start = {screen, 0.0F},
         .end = {screen, viewport.y},
         .rgba = std::abs(x) < spacing * 0.1F ? 0x70815bffU : 0x3038444cU});
  }
  for (float y = firstY; y <= maximum.y; y += spacing) {
    const float screen = projectScenePoint2D(camera, {0.0F, y}, viewport).y;
    lines.push_back(
        {.start = {0.0F, screen},
         .end = {viewport.x, screen},
         .rgba = std::abs(y) < spacing * 0.1F ? 0x705bd77dU : 0x3038444cU});
  }
}

} // namespace

std::vector<EditorOverlayLine2D> buildEditorViewportOverlays2D(
    const runtime::World &world, const EditorSceneView2DCamera &camera,
    const runtime::Vec2 viewportSize,
    const std::unordered_map<std::string, runtime::TilemapAsset2D> &tilemaps,
    const EditorViewportOverlay2DRequest &request) {
  std::vector<EditorOverlayLine2D> lines;
  if (request.grid)
    addGrid(lines, camera, viewportSize);
  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled ||
        !entity.hasComponent<runtime::Transform2DComponent>())
      continue;
    if (request.bounds) {
      if (const auto *sprite = entity.component<runtime::SpriteComponent>()) {
        const runtime::Vec2 size{sprite->size.x > 0.0F ? sprite->size.x : 1.0F,
                                 sprite->size.y > 0.0F ? sprite->size.y : 1.0F};
        addRectangle(lines, world, entity, camera, viewportSize,
                     {-size.x * sprite->pivot.x, -size.y * sprite->pivot.y},
                     {size.x * (1.0F - sprite->pivot.x),
                      size.y * (1.0F - sprite->pivot.y)},
                     0xaaaee35bU);
      }
      if (const auto *tilemap =
              entity.component<runtime::Tilemap2DComponent>()) {
        const auto asset = tilemaps.find(tilemap->asset);
        if (asset != tilemaps.end() && tilemap->pixelsPerUnit > 0.0F) {
          const runtime::Vec2 size{
              asset->second.columns * asset->second.tileWidth /
                  tilemap->pixelsPerUnit,
              asset->second.rows * asset->second.tileHeight /
                  tilemap->pixelsPerUnit};
          addRectangle(lines, world, entity, camera, viewportSize, {}, size,
                       0xaa59d6e8U);
        }
      }
    }
    if (request.cameras) {
      if (const auto *authored =
              entity.component<runtime::Camera2DComponent>()) {
        const float halfHeight = authored->orthographicSize;
        const float halfWidth = halfHeight * std::max(viewportSize.x, 1.0F) /
                                std::max(viewportSize.y, 1.0F);
        addRectangle(lines, world, entity, camera, viewportSize,
                     {-halfWidth, -halfHeight}, {halfWidth, halfHeight},
                     0xaae7a95bU);
      }
    }
  }
  return lines;
}

} // namespace demi::editor
