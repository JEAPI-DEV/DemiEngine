#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/SceneData.h"

#include <raylib.h>

#include <string>
#include <unordered_map>

namespace demi::runtime {

struct DynamicModelCacheEntry {
  std::string signature;
  Model model{};
  bool hasModel = false;
};

class Renderer3D {
public:
  Renderer3D() = default;
  ~Renderer3D();

  Renderer3D(const Renderer3D&) = delete;
  Renderer3D& operator=(const Renderer3D&) = delete;

  void loadTextureAssets(const AssetRegistry& registry);
  void beginFrame(const Camera3DComponent& camera, Vec3 cameraPosition, Vec3 cameraRotation, int width, int height);
  void drawWorld(const World& world);
  void drawHud(const World& world);
  void endFrame();

private:
  Camera3DComponent camera_;
  Vec3 cameraPosition_;
  Vec3 cameraRotation_;
  int width_ = 1;
  int height_ = 1;
  std::unordered_map<std::string, Texture2D> textures_;
  std::unordered_map<std::string, Model> models_;
  std::unordered_map<std::string, Texture2D> modelTextures_;
  std::unordered_map<std::string, DynamicModelCacheEntry> dynamicModels_;
};

} // namespace demi::runtime
