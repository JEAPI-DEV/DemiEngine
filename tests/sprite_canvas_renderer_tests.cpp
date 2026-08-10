#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/SpriteCanvasRenderer.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class CapturingRenderCommands final : public RenderCommands {
public:
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &draw, std::string &) override {
    if (draw.vertices.size() % sizeof(QuadVertex) != 0)
      return false;
    vertices.resize(draw.vertices.size() / sizeof(QuadVertex));
    std::memcpy(vertices.data(), draw.vertices.data(), draw.vertices.size());
    return true;
  }
  bool submit(const BufferedDraw &, std::string &) override { return true; }
  bool submit(const InstancedBufferedDraw &, std::string &) override {
    return true;
  }

  std::vector<QuadVertex> vertices;
};

} // namespace

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
  assert(canvas.initialize(error));
  assert(textures.upload(
      "asset://sprite",
      ImageData2D{.width = 2,
                  .height = 2,
                  .rgba = std::vector<std::byte>(16, std::byte{0xff})},
      error));
  assert(textures.upload(
      "asset://animated#2",
      ImageData2D{.width = 2,
                  .height = 2,
                  .rgba = std::vector<std::byte>(16, std::byte{0xff})},
      error));
  assert(textures.upload(
      "asset://gif#0",
      ImageData2D{.width = 1,
                  .height = 1,
                  .rgba = std::vector<std::byte>(4, std::byte{0xff})},
      error));

  World world;
  Entity textured;
  textured.id = "textured";
  textured.setComponent(Transform2DComponent{
      .position = {1, 2}, .rotation = 0.5F, .scale = {1, 1}});
  textured.setComponent(SpriteComponent{
      .texture = "asset://sprite",
      .size = {2, 1},
      .pivot = {0.25F, 0.75F},
      .flipX = true,
  });
  world.entities.push_back(std::move(textured));
  Entity circle;
  circle.id = "circle";
  circle.setComponent(Transform2DComponent{.position = {-1, 0}});
  circle.setComponent(SpriteComponent{.shape = "circle", .size = {1, 1}});
  world.entities.push_back(std::move(circle));
  Entity animated;
  animated.id = "animated";
  animated.setComponent(Transform2DComponent{.position = {0, -1}});
  animated.setComponent(
      SpriteComponent{.texture = "asset://animated", .size = {1, 1}});
  animated.setComponent(SpriteAnimator2DComponent{.currentFrame = 2});
  world.entities.push_back(std::move(animated));
  Entity gif;
  gif.id = "gif";
  gif.setComponent(Transform2DComponent{.position = {0, 1}});
  gif.setComponent(SpriteComponent{.texture = "asset://gif", .size = {1, 1}});
  world.entities.push_back(std::move(gif));

  assert(canvas.begin(0, 320, 180, 0, error));
  const std::unordered_map<std::string, TextureAnimation2D> animations{
      {"asset://gif", {.frameCount = 1, .frameDurations = {0.1F}}}};
  SpriteCanvasRenderer renderer(canvas, textures, &animations);
  assert(renderer.draw(world, Camera2DComponent{.orthographicSize = 5.0F}, {},
                       320, 180));
  assert(canvas.flush(error));
  assert(canvas.statistics().quads == 3);
  assert(canvas.statistics().triangles >= 38);
  static_cast<void>(graphics.endFrame());

  // Primitive sprites must consume the same Transform2D rotation and scale as
  // their colliders. This is the path used by the FFA shooter's cover walls.
  CapturingRenderCommands capturingCommands;
  Canvas2D capturedCanvas(*resources, capturingCommands);
  assert(capturedCanvas.initialize(error));
  World primitiveWorld;
  Entity cover;
  cover.id = "cover";
  cover.setComponent(
      Transform2DComponent{.rotation = 1.57079632679F, .scale = {2.0F, 0.5F}});
  cover.setComponent(SpriteComponent{.size = {2.0F, 1.0F}});
  primitiveWorld.entities.push_back(std::move(cover));
  assert(capturedCanvas.begin(0, 320, 180, 0, error));
  SpriteCanvasRenderer capturedRenderer(capturedCanvas, textures);
  assert(capturedRenderer.draw(primitiveWorld,
                               Camera2DComponent{.orthographicSize = 5.0F}, {},
                               320, 180));
  assert(capturedCanvas.flush(error));
  assert(capturingCommands.vertices.size() == 4);
  float minX = capturingCommands.vertices.front().x;
  float maxX = minX;
  float minY = capturingCommands.vertices.front().y;
  float maxY = minY;
  for (const QuadVertex &vertex : capturingCommands.vertices) {
    minX = std::min(minX, vertex.x);
    maxX = std::max(maxX, vertex.x);
    minY = std::min(minY, vertex.y);
    maxY = std::max(maxY, vertex.y);
  }
  assert(std::abs((maxX - minX) - 9.0F) < 0.01F);
  assert(std::abs((maxY - minY) - 72.0F) < 0.01F);
  capturedCanvas.shutdown();

  textures.clear();
  canvas.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
