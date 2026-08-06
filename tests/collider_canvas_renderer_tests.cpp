#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx2d/ColliderCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

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
  assert(canvas.initialize(error));

  World world;
  Entity platform;
  platform.id = "platform";
  platform.setComponent(
      Transform2DComponent{.position = {1.0F, -1.0F}, .rotation = 0.25F});
  platform.setComponent(Rigidbody2DComponent{.bodyType = "static"});
  platform.setComponent(BoxCollider2DComponent{
      .size = {4.0F, 0.5F},
      .offset = {0.25F, 0.0F},
      .debugVisible = true,
  });
  world.entities.push_back(std::move(platform));

  Entity circle;
  circle.id = "circle";
  circle.setComponent(Transform2DComponent{.position = {-1.0F, 0.0F}});
  circle.setComponent(CircleCollider2DComponent{
      .radius = 0.5F,
      .offset = {0.1F, 0.2F},
      .debugVisible = true,
  });
  world.entities.push_back(std::move(circle));

  Entity hidden;
  hidden.id = "hidden";
  hidden.setComponent(Transform2DComponent{});
  hidden.setComponent(
      BoxCollider2DComponent{.size = {1.0F, 1.0F}, .debugVisible = false});
  world.entities.push_back(std::move(hidden));

  Entity spriteOwnsVisual;
  spriteOwnsVisual.id = "sprite";
  spriteOwnsVisual.setComponent(Transform2DComponent{});
  spriteOwnsVisual.setComponent(SpriteComponent{.size = {1.0F, 1.0F}});
  spriteOwnsVisual.setComponent(
      BoxCollider2DComponent{.size = {1.0F, 1.0F}, .debugVisible = true});
  world.entities.push_back(std::move(spriteOwnsVisual));

  assert(canvas.begin(0, 320, 180, 0, error));
  ColliderCanvasRenderer renderer(canvas);
  assert(renderer.draw(world, Camera2DComponent{.orthographicSize = 5.0F}, {},
                       320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 37);
  assert(canvas.statistics().triangles == 106);
  static_cast<void>(graphics.endFrame());

  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
