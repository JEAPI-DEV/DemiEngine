#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/SceneData.h"

#include <string>
#include <unordered_map>

namespace demi::runtime {

class Renderer2D {
public:
  explicit Renderer2D(void* nativeRenderer);
  ~Renderer2D();

  Renderer2D(const Renderer2D&) = delete;
  Renderer2D& operator=(const Renderer2D&) = delete;

  void loadTextureAssets(const AssetRegistry& registry);
  void beginFrame(const Camera2DComponent& camera, int width, int height);
  void drawWorld(const World& world);
  void drawHud(const World& world);
  void endFrame();

private:
  void* nativeRenderer_ = nullptr;
  Camera2DComponent camera_;
  int width_ = 1;
  int height_ = 1;
  std::unordered_map<std::string, void*> textures_;
};

} // namespace demi::runtime
