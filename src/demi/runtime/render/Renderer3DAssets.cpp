#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BuiltinRenderShaders.h"
#include "demi/runtime/render/Renderer3D.h"
#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {
namespace {
struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

std::optional<std::string> nextPpmToken(std::istream &input) {
  std::string token;
  while (input >> token) {
    if (!token.empty() && token[0] == '#') {
      std::string ignored;
      std::getline(input, ignored);
      continue;
    }
    return token;
  }
  return std::nullopt;
}

std::optional<ImageData> loadPpm(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }

  const std::optional<std::string> magic = nextPpmToken(input);
  if (!magic.has_value() || *magic != "P3") {
    return std::nullopt;
  }

  const std::optional<std::string> widthText = nextPpmToken(input);
  const std::optional<std::string> heightText = nextPpmToken(input);
  const std::optional<std::string> maxText = nextPpmToken(input);
  if (!widthText.has_value() || !heightText.has_value() ||
      !maxText.has_value()) {
    return std::nullopt;
  }

  ImageData image;
  try {
    image.width = std::stoi(*widthText);
    image.height = std::stoi(*heightText);
    const int maxValue = std::stoi(*maxText);
    if (image.width <= 0 || image.height <= 0 || maxValue <= 0) {
      return std::nullopt;
    }

    image.pixels.reserve(
        static_cast<std::size_t>(image.width * image.height * 4));
    for (int i = 0; i < image.width * image.height; ++i) {
      const std::optional<std::string> rText = nextPpmToken(input);
      const std::optional<std::string> gText = nextPpmToken(input);
      const std::optional<std::string> bText = nextPpmToken(input);
      if (!rText.has_value() || !gText.has_value() || !bText.has_value()) {
        return std::nullopt;
      }

      const auto scale = [maxValue](const int value) {
        return static_cast<unsigned char>(
            std::clamp(value * 255 / maxValue, 0, 255));
      };
      const unsigned char r = scale(std::stoi(*rText));
      const unsigned char g = scale(std::stoi(*gText));
      const unsigned char b = scale(std::stoi(*bText));
      const unsigned char a = 255U;
      image.pixels.push_back(r);
      image.pixels.push_back(g);
      image.pixels.push_back(b);
      image.pixels.push_back(a);
    }
  } catch (...) {
    return std::nullopt;
  }

  return image;
}

Shader loadAlphaCutoutShader() {
#if defined(__ANDROID__)
  const char *vertex = builtin_shaders::AlphaCutoutGlesVertex;
  const char *fragment = builtin_shaders::AlphaCutoutGlesFragment;
#else
  const char *vertex = builtin_shaders::AlphaCutoutVertex;
  const char *fragment = builtin_shaders::AlphaCutoutFragment;
#endif
  Shader shader = LoadShaderFromMemory(vertex, fragment);
  shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
  shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
  shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
  shader.locs[SHADER_LOC_COLOR_DIFFUSE] =
      GetShaderLocation(shader, "colDiffuse");
  return shader;
}

Shader loadPostProcessShader() {
#if defined(__ANDROID__)
  const char *vertex = builtin_shaders::PostProcessGlesVertex;
  const char *fragment = builtin_shaders::PostProcessGlesFragment;
#else
  const char *vertex = builtin_shaders::PostProcessVertex;
  const char *fragment = builtin_shaders::PostProcessFragment;
#endif
  return LoadShaderFromMemory(vertex, fragment);
}

} // namespace
void Renderer3D::unloadAssets() {
  particleSystem_.clear();
  for (auto &[id, model] : dynamicModels_) {
    (void)id;
    if (model.hasModel) {
      UnloadModel(model.model);
    }
  }
  for (auto &[id, texture] : textures_) {
    (void)id;
    const bool ownedByRenderTarget = std::ranges::any_of(
        cameraSurfaces_, [&](const auto &entry) {
          return entry.second.texture.id == texture.id;
        });
    if (!ownedByRenderTarget)
      UnloadTexture(texture);
  }
  for (auto &[id, model] : models_) {
    (void)id;
    UnloadModel(model);
  }
  for (auto &[id, textures] : modelOwnedTextures_) {
    (void)id;
    renderer3d_detail::unloadOwnedTextures(textures);
  }
  for (auto &[id, animatedModel] : animatedModels_) {
    (void)id;
    if (animatedModel.hasModel) {
      UnloadModel(animatedModel.model);
    }
    renderer3d_detail::unloadOwnedTextures(animatedModel.ownedTextures);
  }
  for (auto &[id, animations] : modelAnimations_) {
    (void)id;
    if (animations.clips != nullptr) {
      UnloadModelAnimations(animations.clips, animations.clipCount);
    }
  }
  for (auto &[id, texture] : modelTextures_) {
    (void)id;
    UnloadTexture(texture);
  }
  shaders_.clear();
  if (hasAlphaCutoutShader_) {
    UnloadShader(alphaCutoutShader_);
  }
  if (hasPostProcessShader_)
    UnloadShader(postProcessShader_);
  for (auto &[id, target] : cameraSurfaces_) {
    (void)id;
    UnloadRenderTexture(target);
  }
  dynamicModels_.clear();
  textures_.clear();
  models_.clear();
  modelOwnedTextures_.clear();
  animatedModels_.clear();
  modelAnimations_.clear();
  modelTextures_.clear();
  modelPaths_.clear();
  modelTextureSettings_.clear();
  materials_.clear();
  renderTargets_.clear();
  imageAnimations_.clear();
  gifAnimations_.clear();
  alphaCutoutShader_ = {};
  hasAlphaCutoutShader_ = false;
  postProcessShader_ = {};
  hasPostProcessShader_ = false;
  cameraSurfaces_.clear();
  cameraSurfaceTextureIds_.clear();
  usedCameraSurfaces_.clear();
}

Renderer3D::~Renderer3D() { unloadAssets(); }

void Renderer3D::loadAssets(const AssetRegistry &registry) {
  ProfileScope scope("Renderer3D.load_assets");
  unloadAssets();
  shaders_.load(registry);
  if (!hasAlphaCutoutShader_) {
    alphaCutoutShader_ = loadAlphaCutoutShader();
    hasAlphaCutoutShader_ = alphaCutoutShader_.id != 0;
  }
  if (!hasPostProcessShader_) {
    postProcessShader_ = loadPostProcessShader();
    hasPostProcessShader_ = postProcessShader_.id != 0;
  }
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type == "Material") {
      if (auto material = assets::loadMaterialAsset(asset.sourcePath))
        materials_.emplace(asset.id, std::move(*material));
      continue;
    }
    if (asset.type == "RenderTarget") {
      if (auto target = assets::loadRenderTargetAsset(asset.sourcePath))
        renderTargets_.emplace(asset.id, std::move(*target));
      continue;
    }
    if (asset.type == "Shader") {
      continue;
    }
    if (asset.type == "ImageAnimation2D") {
      bool loaded = true;
      for (std::size_t frame = 0; frame < asset.sourcePaths.size(); ++frame) {
        Texture2D texture =
            LoadTexture(asset.sourcePaths[frame].string().c_str());
        if (texture.id == 0) {
          std::cerr << "Animation frame load failed for " << asset.id
                    << " from " << asset.sourcePaths[frame].string() << ".\n";
          loaded = false;
          break;
        }
        renderer3d_detail::applyTextureSettings(texture, asset.textureSettings,
                                                TEXTURE_FILTER_POINT);
        textures_[asset.id + "#" + std::to_string(frame)] = texture;
      }
      if (loaded) {
        imageAnimations_[asset.id] = static_cast<int>(asset.sourcePaths.size());
      }
      continue;
    }
    if (asset.type == "Model3D") {
      Model model{};
      {
        ProfileScope modelScope("Renderer3D.load_model_asset");
        model = LoadModel(asset.sourcePath.string().c_str());
      }
      if (model.meshCount <= 0) {
        std::cerr << "Model load failed for " << asset.id << " from "
                  << asset.sourcePath.string() << ".\n";
        continue;
      }
      renderer3d_detail::applyModelTextureSettings(model,
                                                   asset.textureSettings);
      modelTextureSettings_[asset.id] = asset.textureSettings;
      modelOwnedTextures_[asset.id] =
          renderer3d_detail::ownedTexturesForModel(model);
      if (asset.texturePath.has_value()) {
        Texture2D texture{};
        {
          ProfileScope textureScope("Renderer3D.load_model_texture");
          texture = LoadTexture(asset.texturePath->string().c_str());
        }
        if (texture.id == 0) {
          std::cerr << "Model texture load failed for " << asset.id << " from "
                    << asset.texturePath->string() << ".\n";
        } else if (model.materialCount > 0) {
          renderer3d_detail::applyTextureSettings(
              texture, asset.textureSettings, TEXTURE_FILTER_BILINEAR);
          SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE,
                             texture);
          for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
            model.meshMaterial[meshIndex] = 0;
          }
          modelTextures_[asset.id] = texture;
        } else {
          UnloadTexture(texture);
        }
      }
      models_[asset.id] = model;
      modelPaths_[asset.id] = asset.sourcePath;
      int clipCount = 0;
      ModelAnimation *clips =
          LoadModelAnimations(asset.sourcePath.string().c_str(), &clipCount);
      if (clips != nullptr && clipCount > 0) {
        ModelAnimationAsset animationAsset{
            .clips = clips, .clipCount = clipCount, .clipsByName = {}};
        for (int clip = 0; clip < clipCount; ++clip) {
          const std::string name = clips[clip].name;
          animationAsset.clipsByName["clip_" + std::to_string(clip)] = clip;
          if (!name.empty())
            animationAsset.clipsByName[name] = clip;
        }
        modelAnimations_[asset.id] = std::move(animationAsset);
      }
      continue;
    }

    if (asset.type != "Texture2D") {
      continue;
    }

    Texture2D texture{};
    if (const std::optional<ImageData> image = loadPpm(asset.sourcePath)) {
      ::Image rlImage{
          .data = const_cast<void *>(
              static_cast<const void *>(image->pixels.data())),
          .width = image->width,
          .height = image->height,
          .mipmaps = 1,
          .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
      };
      {
        ProfileScope textureScope("Renderer3D.load_texture_from_image");
        texture = LoadTextureFromImage(rlImage);
      }
    } else {
      {
        ProfileScope textureScope("Renderer3D.load_texture_file");
        texture = LoadTexture(asset.sourcePath.string().c_str());
      }
    }
    if (texture.id == 0) {
      std::cerr << "Texture load failed for " << asset.id << " from "
                << asset.sourcePath.string() << ". Using fallback material.\n";
      continue;
    }

    renderer3d_detail::applyTextureSettings(texture, asset.textureSettings,
                                            TEXTURE_FILTER_POINT);
    textures_[asset.id] = texture;
  }
}
} // namespace demi::runtime
