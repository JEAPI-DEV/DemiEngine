#include "demi/runtime/Renderer2D.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#if DEMI_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace demi::runtime {

namespace {

#if DEMI_HAS_SDL3

Uint8 colorChannel(float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  return static_cast<Uint8>(std::round(clamped * 255.0F));
}

struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<Uint32> pixels;
};

std::optional<std::string> nextPpmToken(std::istream& input) {
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

std::optional<ImageData> loadPpm(const std::filesystem::path& path) {
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
  if (!widthText.has_value() || !heightText.has_value() || !maxText.has_value()) {
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

    image.pixels.reserve(static_cast<std::size_t>(image.width * image.height));
    for (int i = 0; i < image.width * image.height; ++i) {
      const std::optional<std::string> rText = nextPpmToken(input);
      const std::optional<std::string> gText = nextPpmToken(input);
      const std::optional<std::string> bText = nextPpmToken(input);
      if (!rText.has_value() || !gText.has_value() || !bText.has_value()) {
        return std::nullopt;
      }

      const auto scale = [maxValue](const int value) {
        return static_cast<Uint32>(std::clamp(value * 255 / maxValue, 0, 255));
      };
      const Uint32 r = scale(std::stoi(*rText));
      const Uint32 g = scale(std::stoi(*gText));
      const Uint32 b = scale(std::stoi(*bText));
      image.pixels.push_back((r << 24U) | (g << 16U) | (b << 8U) | 255U);
    }
  } catch (...) {
    return std::nullopt;
  }

  return image;
}

float pixelsPerUnit(const Camera2DComponent& camera, const int height) {
  return static_cast<float>(height) / std::max(camera.orthographicSize * 2.0F, 1.0F);
}

float worldToScreenX(const Camera2DComponent& camera, const int width, const int height, const float x) {
  return static_cast<float>(width) * 0.5F + x * pixelsPerUnit(camera, height);
}

float worldToScreenY(const Camera2DComponent& camera, const int width, const int height, const float y) {
  (void)width;
  return static_cast<float>(height) * 0.5F - y * pixelsPerUnit(camera, height);
}

void drawIsoDiamond(SDL_Renderer* renderer, const Camera2DComponent& camera, const int width, const int height, const float x, const float y, const float cells) {
  const float centerX = worldToScreenX(camera, width, height, x);
  const float centerY = worldToScreenY(camera, width, height, y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfW = cells * ppu * 0.65F;
  const float halfH = cells * ppu * 0.32F;
  SDL_RenderLine(renderer, centerX, centerY - halfH, centerX + halfW, centerY);
  SDL_RenderLine(renderer, centerX + halfW, centerY, centerX, centerY + halfH);
  SDL_RenderLine(renderer, centerX, centerY + halfH, centerX - halfW, centerY);
  SDL_RenderLine(renderer, centerX - halfW, centerY, centerX, centerY - halfH);
}

void drawEntity(SDL_Renderer* renderer,
                const std::unordered_map<std::string, void*>& textures,
                const Camera2DComponent& camera,
                const Entity& entity,
                const int width,
                const int height) {
  if (entity.isoGrid.has_value()) {
    SDL_SetRenderDrawColor(renderer, 55, 88, 78, 255);
    for (int i = -8; i <= 8; ++i) {
      drawIsoDiamond(renderer, camera, width, height, static_cast<float>(i), 0.0F, 1.0F);
      drawIsoDiamond(renderer, camera, width, height, 0.0F, static_cast<float>(i), 1.0F);
    }
    return;
  }

  Vec2 position;
  Vec2 size = {1.0F, 1.0F};
  if (entity.transform2D.has_value()) {
    position = entity.transform2D->position;
  }
  if (entity.isoTransform.has_value()) {
    position = {entity.isoTransform->tile.x - 16.0F, entity.isoTransform->tile.y - 16.0F};
    size = entity.isoTransform->footprint;
  }
  if (entity.hitboxController.has_value()) {
    size = entity.hitboxController->hurtbox;
  }

  const float ppu = pixelsPerUnit(camera, height);
  const float entityWidth = std::max(size.x * ppu, 24.0F);
  const float entityHeight = std::max(size.y * ppu, 24.0F);
  SDL_FRect rect{
    .x = worldToScreenX(camera, width, height, position.x) - entityWidth * 0.5F,
    .y = worldToScreenY(camera, width, height, position.y) - entityHeight * 0.5F,
    .w = entityWidth,
    .h = entityHeight,
  };

  if (entity.sprite.has_value()) {
    const auto texture = textures.find(entity.sprite->texture);
    if (texture != textures.end()) {
      SDL_RenderTexture(renderer, static_cast<SDL_Texture*>(texture->second), nullptr, &rect);
      SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
      SDL_RenderRect(renderer, &rect);
      return;
    }
    SDL_SetRenderDrawColor(renderer, 92, 172, 238, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 222, 166, 82, 255);
  }
  SDL_RenderFillRect(renderer, &rect);

  if (entity.hitboxController.has_value()) {
    SDL_SetRenderDrawColor(renderer, 244, 91, 105, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
  }
  SDL_RenderRect(renderer, &rect);
}

#endif

} // namespace

Renderer2D::Renderer2D(void* nativeRenderer) : nativeRenderer_(nativeRenderer) {}

Renderer2D::~Renderer2D() {
#if DEMI_HAS_SDL3
  for (auto& [id, texture] : textures_) {
    (void)id;
    SDL_DestroyTexture(static_cast<SDL_Texture*>(texture));
  }
#endif
}

void Renderer2D::loadTextureAssets(const AssetRegistry& registry) {
#if DEMI_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(nativeRenderer_);
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "Texture2D") {
      continue;
    }

    const std::optional<ImageData> image = loadPpm(asset.sourcePath);
    if (!image.has_value()) {
      std::cerr << "Texture load failed for " << asset.id << " from " << asset.sourcePath.string() << ". Using fallback rectangle.\n";
      continue;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, image->width, image->height);
    if (texture == nullptr) {
      std::cerr << "SDL_CreateTexture failed for " << asset.id << ": " << SDL_GetError() << '\n';
      continue;
    }

    if (!SDL_UpdateTexture(texture, nullptr, image->pixels.data(), image->width * static_cast<int>(sizeof(Uint32)))) {
      std::cerr << "SDL_UpdateTexture failed for " << asset.id << ": " << SDL_GetError() << '\n';
      SDL_DestroyTexture(texture);
      continue;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    textures_[asset.id] = texture;
  }
#else
  (void)registry;
#endif
}

void Renderer2D::beginFrame(const Camera2DComponent& camera, const int width, const int height) {
  camera_ = camera;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

#if DEMI_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(nativeRenderer_);
  SDL_SetRenderDrawColor(renderer,
                         colorChannel(camera.clearColor.r),
                         colorChannel(camera.clearColor.g),
                         colorChannel(camera.clearColor.b),
                         colorChannel(camera.clearColor.a));
  SDL_RenderClear(renderer);
#endif
}

void Renderer2D::drawWorld(const World& world) {
#if DEMI_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(nativeRenderer_);
  for (const Entity& entity : world.entities) {
    if (entity.sprite.has_value() || entity.hitboxController.has_value() || entity.isoGrid.has_value() || entity.buildable.has_value()) {
      drawEntity(renderer, textures_, camera_, entity, width_, height_);
    }
  }
#else
  (void)world;
#endif
}

void Renderer2D::endFrame() {
#if DEMI_HAS_SDL3
  SDL_RenderPresent(static_cast<SDL_Renderer*>(nativeRenderer_));
#endif
}

} // namespace demi::runtime
