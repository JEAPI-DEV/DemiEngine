#pragma once

#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/render/RendererAssetData.h"
#include "demi/runtime/render/ParticleSystem3D.h"
#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/scene/model/World.h"

#include <raylib.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct PostProcessStackComponent;

struct DynamicModelCacheEntry {
  std::string signature;
  std::string texture;
  std::uint64_t revision = 0;
  Model model{};
  bool hasModel = false;
};

struct ModelAnimationAsset {
  ModelAnimation *clips = nullptr;
  int clipCount = 0;
  std::unordered_map<std::string, int> clipsByName;
};

struct AnimatedModelCacheEntry {
  std::string assetId;
  Model model{};
  std::vector<Texture2D> ownedTextures;
  bool hasModel = false;
};

class Renderer3D {
public:
  Renderer3D() = default;
  ~Renderer3D();

  Renderer3D(const Renderer3D &) = delete;
  Renderer3D &operator=(const Renderer3D &) = delete;

  void loadTextureAssets(const AssetRegistry &registry);
  void beginFrame(int width, int height, Color clearColor);
  void beginCamera(const std::string &cameraId,
                   const Camera3DComponent &camera, Vec3 cameraPosition,
                   Vec3 cameraForward, Vec3 cameraUp);
  void drawWorld(World &world, float deltaTime);
  void endCamera(const PostProcessStackComponent *postProcess = nullptr,
                 const World *hudTarget = nullptr);
  void presentCamera(const std::string &cameraId,
                     const Camera3DComponent &camera,
                     const PostProcessStackComponent *postProcess = nullptr);
  [[nodiscard]] bool
  canPresentCamera(const std::string &cameraId,
                   const Camera3DComponent &camera) const;
  void drawHud(const World &world);
  void endFrame();
  [[nodiscard]] const RenderStatistics &statistics() const {
    return statistics_;
  }

private:
  void configureCameraOutput(const std::string &cameraId,
                             const Camera3DComponent &camera);
  void presentCurrentCamera(
      const PostProcessStackComponent *postProcess = nullptr);
  void unloadAssets();

  Camera3DComponent camera_;
  Vec3 cameraPosition_;
  Vec3 cameraForward_;
  Vec3 cameraUp_;
  int width_ = 1;
  int height_ = 1;
  int frameWidth_ = 1;
  int frameHeight_ = 1;
  Rectangle cameraDestination_{};
  std::string currentCameraId_;
  std::unordered_map<std::string, Texture2D> textures_;
  std::unordered_map<std::string, assets::MaterialAsset> materials_;
  std::unordered_map<std::string, assets::RenderTargetAsset> renderTargets_;
  std::unordered_map<std::string, Shader> materialShaders_;
  std::unordered_map<std::string, int> imageAnimations_;
  std::unordered_map<std::string, GifAnimationTextureData> gifAnimations_;
  float animationTime_ = 0.0F;
  std::unordered_map<std::string, Model> models_;
  std::unordered_map<std::string, std::vector<Texture2D>> modelOwnedTextures_;
  std::unordered_map<std::string, std::filesystem::path> modelPaths_;
  std::unordered_map<std::string, Texture2D> modelTextures_;
  std::unordered_map<std::string, TextureImporterSettings>
      modelTextureSettings_;
  std::unordered_map<std::string, ModelAnimationAsset> modelAnimations_;
  std::unordered_map<std::string, AnimatedModelCacheEntry> animatedModels_;
  std::unordered_map<std::string, DynamicModelCacheEntry> dynamicModels_;
  Shader alphaCutoutShader_{};
  bool hasAlphaCutoutShader_ = false;
  Shader postProcessShader_{};
  bool hasPostProcessShader_ = false;
  std::unordered_map<std::string, RenderTexture2D> cameraSurfaces_;
  std::unordered_map<std::string, std::string> cameraSurfaceTextureIds_;
  std::unordered_set<std::string> usedCameraSurfaces_;
  ::Camera3D raylibCamera_{};
  ParticleSystem3D particleSystem_;
  bool particlesUpdated_ = false;
  RenderStatistics statistics_;
};

} // namespace demi::runtime
