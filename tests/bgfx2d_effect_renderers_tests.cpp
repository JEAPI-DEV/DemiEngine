#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/DebugCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/ParticleCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <array>
#include <cassert>
#include <string>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 320,
                                                  .height = 180,
                                                  .vsync = false},
                             error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  Canvas2D canvas(*resources, *commands);
  TextureLibrary2D textures(*resources);
  FontAtlas2D font(*resources);
  assert(canvas.initialize(error));
  assert(font.initializeBuiltin(24, error));
  assert(textures.upload(
      "asset://particle",
      ImageData2D{.width = 2,
                  .height = 2,
                  .rgba = std::vector<std::byte>(16, std::byte{0xff})},
      error));

  const Camera2DComponent camera{.orthographicSize = 5.0F};
  const std::array particles{
      ParticleRenderData2D{.position = {-1, 0},
                           .size = 0.5F,
                           .rotationRadians = 0.3F,
                           .color = {1, 0, 0, 1},
                           .texture = "asset://particle",
                           .sortingOrder = 2},
      ParticleRenderData2D{.position = {1, 0},
                           .size = 0.25F,
                           .color = {0, 1, 0, 1},
                           .sortingOrder = 1},
      ParticleRenderData2D{.position = {}, .size = 0.0F, .color = {1, 1, 1, 1}},
  };

  World world;
  world.debug = {.colliders = true,
                 .contacts = true,
                 .entityIds = true,
                 .drawOrder = true};
  world.debugFocusedEntityId = "box";
  world.debugFocusRequired = true;
  world.debugLines.push_back(
      {.start = {-2, -2}, .end = {2, 2}, .color = {1, 1, 0, 1}, .width = 3});
  Entity box;
  box.id = "box";
  box.setComponent(Transform2DComponent{.position = {-1, 0}, .rotation = 0.5F});
  box.setComponent(
      BoxCollider2DComponent{.size = {2, 1}, .offset = {0.25F, 0}});
  world.entities.push_back(std::move(box));
  Entity circle;
  circle.id = "circle";
  circle.setComponent(Transform2DComponent{.position = {1, 0}});
  circle.setComponent(
      CircleCollider2DComponent{.radius = 0.5F, .offset = {0, 0.25F}});
  world.entities.push_back(std::move(circle));
  world.physicsContacts.push_back({.entityId = "circle", .normal = {-1, 0}});

  navigation::NavigationGrid2D grid;
  assert(grid.configure(2, 2, 1.0F, {-1, -1}));
  assert(grid.setBlocked({0, 0}, true));
  assert(grid.setCost({1, 1}, 2.0F));

  assert(canvas.begin(0, 320, 180, 0, error));
  ParticleCanvasRenderer particlesRenderer(canvas, textures);
  DebugCanvasRenderer debugRenderer(canvas, &font);
  assert(particlesRenderer.draw(particles, camera, {}, 320, 180));
  assert(debugRenderer.drawWorld(world, camera, {}, 320, 180));
  assert(debugRenderer.drawNavigation(grid, camera, {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads > 20);
  assert(canvas.statistics().triangles > 40);
  static_cast<void>(graphics.endFrame());

  // Disabled and invisible colliders must not leak into debug presentation.
  world.entities.front().enabled = false;
  world.entities.back().component<CircleCollider2DComponent>()->debugVisible =
      false;
  world.debugLines.clear();
  world.physicsContacts.clear();
  world.debug.entityIds = false;
  world.debug.drawOrder = false;
  world.debugFocusedEntityId.clear();
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(debugRenderer.drawWorld(world, camera, {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 0);
  static_cast<void>(graphics.endFrame());

  // Global ID/draw-order toggles require an explicit runtime hierarchy focus;
  // an empty focus must never flood the viewport with every entity label.
  world.debug.entityIds = true;
  world.debug.drawOrder = true;
  world.debugFocusRequired = true;
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(debugRenderer.drawWorld(world, camera, {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 0);
  static_cast<void>(graphics.endFrame());

  // Non-editor runtime/CLI overlays retain their global entity-ID behavior.
  world.debugFocusRequired = false;
  world.debug.drawOrder = false;
  assert(canvas.begin(0, 320, 180, 0, error));
  assert(debugRenderer.drawWorld(world, camera, {}, 320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads > 0);
  static_cast<void>(graphics.endFrame());

  font.shutdown();
  textures.clear();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
