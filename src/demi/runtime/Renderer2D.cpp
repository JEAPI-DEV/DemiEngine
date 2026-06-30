#include "demi/runtime/Renderer2D.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
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

using Glyph = std::array<std::string_view, 7>;

Glyph glyphFor(const char raw) {
  const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
  switch (c) {
  case '0': return {"111", "101", "101", "101", "101", "101", "111"};
  case '1': return {"010", "110", "010", "010", "010", "010", "111"};
  case '2': return {"111", "001", "001", "111", "100", "100", "111"};
  case '3': return {"111", "001", "001", "111", "001", "001", "111"};
  case '4': return {"101", "101", "101", "111", "001", "001", "001"};
  case '5': return {"111", "100", "100", "111", "001", "001", "111"};
  case '6': return {"111", "100", "100", "111", "101", "101", "111"};
  case '7': return {"111", "001", "001", "010", "010", "010", "010"};
  case '8': return {"111", "101", "101", "111", "101", "101", "111"};
  case '9': return {"111", "101", "101", "111", "001", "001", "111"};
  case 'A': return {"010", "101", "101", "111", "101", "101", "101"};
  case 'D': return {"110", "101", "101", "101", "101", "101", "110"};
  case 'I': return {"111", "010", "010", "010", "010", "010", "111"};
  case 'N': return {"101", "111", "111", "111", "101", "101", "101"};
  case 'O': return {"111", "101", "101", "101", "101", "101", "111"};
  case 'P': return {"110", "101", "101", "110", "100", "100", "100"};
  case 'S': return {"111", "100", "100", "111", "001", "001", "111"};
  case 'T': return {"111", "010", "010", "010", "010", "010", "010"};
  case ':': return {"000", "010", "010", "000", "010", "010", "000"};
  case '-': return {"000", "000", "000", "111", "000", "000", "000"};
  default: return {"000", "000", "000", "000", "000", "000", "000"};
  }
}

void drawText(SDL_Renderer* renderer, const HudTextElement& element) {
  if (!element.visible) {
    return;
  }

  const float pixel = std::max(element.scale, 1.0F);
  float x = element.position.x;
  const float y = element.position.y;
  SDL_SetRenderDrawColor(renderer,
                         colorChannel(element.color.r),
                         colorChannel(element.color.g),
                         colorChannel(element.color.b),
                         colorChannel(element.color.a));

  for (const char c : element.text) {
    if (c == ' ') {
      x += pixel * 4.0F;
      continue;
    }

    const Glyph glyph = glyphFor(c);
    for (std::size_t row = 0; row < glyph.size(); ++row) {
      for (std::size_t col = 0; col < glyph[row].size(); ++col) {
        if (glyph[row][col] != '1') {
          continue;
        }
        SDL_FRect rect{
          .x = x + static_cast<float>(col) * pixel,
          .y = y + static_cast<float>(row) * pixel,
          .w = pixel,
          .h = pixel,
        };
        SDL_RenderFillRect(renderer, &rect);
      }
    }
    x += pixel * 4.0F;
  }
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
      const Uint32 a = (r == 0U && g == 0U && b == 0U) ? 0U : 255U;
      image.pixels.push_back((r << 24U) | (g << 16U) | (b << 8U) | a);
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

Vec2 isoTileToWorld(const Vec2 tile, const float gridWidth = 32.0F, const float gridHeight = 32.0F) {
  return Vec2{
    .x = (tile.x - tile.y) * 0.65F,
    .y = -((tile.x + tile.y) - ((gridWidth + gridHeight) * 0.5F)) * 0.32F,
  };
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

void drawIsoFootprint(SDL_Renderer* renderer,
                      const Camera2DComponent& camera,
                      const int width,
                      const int height,
                      const Vec2 tile,
                      const Vec2 footprint,
                      const Vec2 gridSize) {
  const Vec2 centerTile{.x = tile.x + (footprint.x - 1.0F) * 0.5F, .y = tile.y + (footprint.y - 1.0F) * 0.5F};
  const Vec2 center = isoTileToWorld(centerTile, gridSize.x, gridSize.y);
  const float centerX = worldToScreenX(camera, width, height, center.x);
  const float centerY = worldToScreenY(camera, width, height, center.y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfW = std::max((footprint.x + footprint.y) * ppu * 0.33F, 12.0F);
  const float halfH = std::max((footprint.x + footprint.y) * ppu * 0.16F, 6.0F);

  SDL_SetRenderDrawColor(renderer, 42, 88, 78, 180);
  SDL_RenderLine(renderer, centerX, centerY - halfH, centerX + halfW, centerY);
  SDL_RenderLine(renderer, centerX + halfW, centerY, centerX, centerY + halfH);
  SDL_RenderLine(renderer, centerX, centerY + halfH, centerX - halfW, centerY);
  SDL_RenderLine(renderer, centerX - halfW, centerY, centerX, centerY - halfH);
}

void drawEntity(SDL_Renderer* renderer,
                const std::unordered_map<std::string, void*>& textures,
                const Camera2DComponent& camera,
                const Entity& entity,
                const Vec2 isoGridSize,
                const int width,
                const int height) {
  if (entity.isoGrid.has_value()) {
    SDL_SetRenderDrawColor(renderer, 55, 88, 78, 255);
    for (int y = 0; y < entity.isoGrid->height; ++y) {
      for (int x = 0; x < entity.isoGrid->width; ++x) {
        const Vec2 world = isoTileToWorld(Vec2{.x = static_cast<float>(x), .y = static_cast<float>(y)},
                                          static_cast<float>(entity.isoGrid->width),
                                          static_cast<float>(entity.isoGrid->height));
        drawIsoDiamond(renderer, camera, width, height, world.x, world.y, 0.5F);
      }
    }
    return;
  }

  Vec2 position;
  Vec2 size = {1.0F, 1.0F};
  if (entity.transform2D.has_value()) {
    position = entity.transform2D->position;
  }
  if (entity.isoTransform.has_value()) {
    position = isoTileToWorld(entity.isoTransform->tile, isoGridSize.x, isoGridSize.y);
    position.y += entity.isoTransform->height;
    size = entity.isoTransform->footprint;
  }
  if (entity.hitboxController.has_value()) {
    size = entity.hitboxController->hurtbox;
  }
  if (entity.boxCollider2D.has_value()) {
    size = entity.boxCollider2D->size;
    if (entity.transform2D.has_value()) {
      position.x += entity.boxCollider2D->offset.x;
      position.y += entity.boxCollider2D->offset.y;
    }
  }

  const float ppu = pixelsPerUnit(camera, height);
  float entityWidth = std::max(size.x * ppu, 24.0F);
  float entityHeight = std::max(size.y * ppu, 24.0F);
  if (entity.buildable.has_value() && entity.isoTransform.has_value()) {
    entityWidth = std::max((size.x + size.y) * ppu * 0.62F, 22.0F);
    entityHeight = std::max((size.x + size.y) * ppu * 0.58F, 22.0F);
  }

  if (entity.buildable.has_value() && entity.isoTransform.has_value()) {
    drawIsoFootprint(renderer, camera, width, height, entity.isoTransform->tile, entity.isoTransform->footprint, isoGridSize);
  }

  const float screenX = worldToScreenX(camera, width, height, position.x);
  const float screenY = worldToScreenY(camera, width, height, position.y);
  SDL_FRect rect{
    .x = screenX - entityWidth * 0.5F,
    .y = screenY - entityHeight * 0.5F,
    .w = entityWidth,
    .h = entityHeight,
  };
  if (entity.buildable.has_value() && entity.isoTransform.has_value()) {
    rect.y = screenY - entityHeight + std::max((size.x + size.y) * ppu * 0.10F, 4.0F);
  }

  if (entity.sprite.has_value()) {
    const auto texture = textures.find(entity.sprite->texture);
    if (texture != textures.end()) {
      SDL_RenderTexture(renderer, static_cast<SDL_Texture*>(texture->second), nullptr, &rect);
      return;
    }
    SDL_SetRenderDrawColor(renderer, 92, 172, 238, 255);
  } else if (entity.buildable.has_value()) {
    const auto texture = textures.find(entity.buildable->asset);
    if (texture != textures.end()) {
      SDL_RenderTexture(renderer, static_cast<SDL_Texture*>(texture->second), nullptr, &rect);
      return;
    }
    SDL_SetRenderDrawColor(renderer, 186, 128, 55, 255);
  } else if (entity.rigidbody2D.has_value() && entity.rigidbody2D->bodyType == "static") {
    SDL_SetRenderDrawColor(renderer, 96, 136, 96, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 222, 166, 82, 255);
  }
  SDL_RenderFillRect(renderer, &rect);

  if (entity.hitboxController.has_value() || entity.boxCollider2D.has_value()) {
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
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
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
  Vec2 isoGridSize{.x = 32.0F, .y = 32.0F};
  for (const Entity& entity : world.entities) {
    if (entity.isoGrid.has_value()) {
      isoGridSize = Vec2{.x = static_cast<float>(entity.isoGrid->width), .y = static_cast<float>(entity.isoGrid->height)};
      break;
    }
  }

  std::vector<const Entity*> renderables;
  for (const Entity& entity : world.entities) {
    if (entity.sprite.has_value() || entity.hitboxController.has_value() || entity.isoGrid.has_value() || entity.buildable.has_value() || entity.boxCollider2D.has_value()) {
      renderables.push_back(&entity);
    }
  }

  std::ranges::stable_sort(renderables, [](const Entity* left, const Entity* right) {
    const auto depth = [](const Entity* entity) {
      if (entity->isoGrid.has_value()) {
        return -10000.0F;
      }
      if (entity->isoTransform.has_value()) {
        return entity->isoTransform->tile.x + entity->isoTransform->tile.y + entity->isoTransform->height;
      }
      return 0.0F;
    };
    return depth(left) < depth(right);
  });

  for (const Entity* entity : renderables) {
    drawEntity(renderer, textures_, camera_, *entity, isoGridSize, width_, height_);
  }

  for (const DebugLine& line : world.debugLines) {
    SDL_SetRenderDrawColor(renderer,
                           colorChannel(line.color.r),
                           colorChannel(line.color.g),
                           colorChannel(line.color.b),
                           colorChannel(line.color.a));
    SDL_RenderLine(renderer,
                   worldToScreenX(camera_, width_, height_, line.start.x),
                   worldToScreenY(camera_, width_, height_, line.start.y),
                   worldToScreenX(camera_, width_, height_, line.end.x),
                   worldToScreenY(camera_, width_, height_, line.end.y));
  }
#else
  (void)world;
#endif
}

void Renderer2D::drawHud(const World& world) {
#if DEMI_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(nativeRenderer_);
  for (const HudTextElement& element : world.hudText) {
    drawText(renderer, element);
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
