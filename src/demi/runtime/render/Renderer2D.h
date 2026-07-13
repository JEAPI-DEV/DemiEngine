#pragma once

#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/RendererAssetData.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <raylib.h>

#include <string>
#include <unordered_map>

namespace demi::runtime {

class Renderer2D {
public:
  Renderer2D() = default;
  ~Renderer2D();

  Renderer2D(const Renderer2D &) = delete;
  Renderer2D &operator=(const Renderer2D &) = delete;

  void loadTextureAssets(const AssetRegistry &registry);
  void beginFrame(const Camera2DComponent &camera, Vec2 cameraPosition,
                  int width, int height);
  void drawWorld(const World &world);
  void drawHud(const World &world);
  void endFrame();

private:
  Camera2DComponent camera_;
  Vec2 cameraPosition_;
  int width_ = 1;
  int height_ = 1;
  std::unordered_map<std::string, Texture2D> textures_;
  std::unordered_map<std::string, ImageAnimationTextureData> imageAnimations_;
  std::unordered_map<std::string, GifAnimationTextureData> gifAnimations_;
  std::unordered_map<std::string, TilemapAsset2D> tilemaps_;
  float animationTime_ = 0.0F;
};

} // namespace demi::runtime
