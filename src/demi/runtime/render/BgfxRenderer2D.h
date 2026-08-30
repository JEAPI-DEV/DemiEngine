#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/render/backend/FontAtlas2D.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx2d/TextureAnimation2D.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

// Runtime-facing composition root for the bgfx 2D path. Specialized
// renderers remain independently testable; this class only sequences them and
// owns their shared GPU resources.
class BgfxRenderer2D {
public:
  BgfxRenderer2D(GpuResources &resources, RenderCommands &commands);
  ~BgfxRenderer2D();

  BgfxRenderer2D(const BgfxRenderer2D &) = delete;
  BgfxRenderer2D &operator=(const BgfxRenderer2D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics);
  [[nodiscard]] bool beginFrame(const Camera2DComponent &camera,
                                Vec2 cameraPosition,
                                std::uint16_t viewportWidth,
                                std::uint16_t viewportHeight,
                                float deltaSeconds, std::string &error,
                                float physicsInterpolationAlpha = 1.0F);
  [[nodiscard]] bool beginFrameRegion(const Camera2DComponent &camera,
                                      Vec2 cameraPosition, std::uint16_t viewId,
                                      std::uint16_t x, std::uint16_t y,
                                      std::uint16_t viewportWidth,
                                      std::uint16_t viewportHeight,
                                      float deltaSeconds, std::string &error,
                                      float physicsInterpolationAlpha = 1.0F,
                                      FrameBufferHandle frameBuffer = {});
  [[nodiscard]] bool beginOverlay(std::uint16_t viewId,
                                  std::uint16_t viewportWidth,
                                  std::uint16_t viewportHeight,
                                  float deltaSeconds, std::string &error);
  [[nodiscard]] bool beginOverlayRegion(std::uint16_t viewId, std::uint16_t x,
                                        std::uint16_t y,
                                        std::uint16_t viewportWidth,
                                        std::uint16_t viewportHeight,
                                        float deltaSeconds, std::string &error,
                                        FrameBufferHandle frameBuffer = {});
  void setExternalTexture(std::string id, TextureView2D texture);
  [[nodiscard]] bool drawWorld(const World &world,
                               bool forceColliderDebug = false);
  [[nodiscard]] bool drawNavigation(const navigation::NavigationGrid2D &grid);
  [[nodiscard]] bool drawHud(const World &world);
  [[nodiscard]] bool drawUi(const ui::UiDocument &document);
  [[nodiscard]] bool endFrame(std::string &error);

  [[nodiscard]] const Canvas2DStatistics &statistics() const {
    return canvas_.statistics();
  }
  [[nodiscard]] std::size_t loadedTextureCount() const {
    return textures_.size();
  }
  [[nodiscard]] bool hasTexture(const std::string_view id) const {
    return static_cast<bool>(textures_.find(id).handle);
  }

private:
  Canvas2D canvas_;
  FontAtlas2D font_;
  TextureLibrary2D textures_;
  MaterialLibrary materials_;
  std::unordered_map<std::string, TextureView2D> externalTextures_;
  std::unordered_map<std::string, TilemapAsset2D> tilemaps_;
  std::unordered_map<std::string, TextureAnimation2D> textureAnimations_;
  class ParticleSimulation;
  std::unique_ptr<ParticleSimulation> particles_;
  Camera2DComponent camera_;
  Vec2 cameraPosition_;
  std::uint16_t viewportWidth_ = 1;
  std::uint16_t viewportHeight_ = 1;
  float deltaSeconds_ = 0.0F;
  float animationTime_ = 0.0F;
  float physicsInterpolationAlpha_ = 1.0F;
  bool initialized_ = false;
  bool frameOpen_ = false;
};

} // namespace demi::runtime::render
