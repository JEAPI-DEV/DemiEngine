#include "demi/runtime/Renderer2D.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace demi::runtime {

namespace {

// A glyph is a 4x4 pixel grid represented as 4 strings of 4 characters ('0' or '1').
using Glyph = std::array<std::string, 4>;

// Returns a 4x4 bitmap for a printable ASCII character (32–126).
Glyph glyphFor(char c) {
    // Define a few basic characters; you can expand this as needed.
    // For now, we provide a minimal set to avoid linker errors.
    // The actual engine likely expects a full ASCII table.
    switch (c) {
        case 'A': return { "0110", "1001", "1111", "1001" };
        case 'B': return { "1110", "1001", "1110", "1001" };
        case 'C': return { "0110", "1000", "1000", "0110" };
        // Add more characters as needed. For simplicity, fallback to a rectangle.
        default:  return { "1111", "1001", "1001", "1111" };
    }
}

} // namespace
namespace {

::Color toRlColor(const Color& value) {
  return ::Color{
    static_cast<unsigned char>(std::round(std::clamp(value.r, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.g, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.b, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.a, 0.0F, 1.0F) * 255.0F)),
  };
}

const ::Color WhiteTint = {255, 255, 255, 255};
}

void drawText(const HudTextElement& element, float scaleX, float scaleY) {
    constexpr float HudFontBaseSize = 8.0F;
    constexpr float HudFontMinSize = 4.0F;
    constexpr float HudLetterSpacing = 5.0F;
    if (!element.visible || element.text.empty()) return;
    float fontSize = std::max(element.scale * HudFontBaseSize * std::min(scaleX, scaleY), HudFontMinSize);
    Vector2 pos{element.position.x * scaleX, element.position.y * scaleY};
    DrawTextEx(GetFontDefault(), element.text.c_str(), pos, fontSize, HudLetterSpacing, toRlColor(element.color));
}
void drawHudRect(const HudRectElement& element, float scaleX, float scaleY) {
    if (!element.visible) {
        return;
    }

    Rectangle rect{
        .x = element.position.x * scaleX,
        .y = element.position.y * scaleY,
        .width = element.size.x * scaleX,
        .height = element.size.y * scaleY,
    };

    DrawRectangleRec(rect, toRlColor(element.color));
}

void drawHudButton(const HudButtonElement& element, float scaleX, float scaleY) {
    constexpr float HudFontBaseSize = 8.0F;
    constexpr float HudFontMinSize = 4.0F;
    constexpr float HudLetterSpacing = 5.0F;

    if (!element.visible) {
        return;
    }

    Rectangle rect{
        .x = element.position.x * scaleX,
        .y = element.position.y * scaleY,
        .width = element.size.x * scaleX,
        .height = element.size.y * scaleY,
    };

    DrawRectangleRec(
        rect,
        toRlColor(element.hovered ? element.hoverColor : element.color)
    );

    if (!element.label.empty()) {
        const float textScale = std::min(scaleX, scaleY);
        const float fontSize = std::max(element.scale * HudFontBaseSize * textScale, HudFontMinSize);

        Font font = GetFontDefault();
        Vector2 textSize = MeasureTextEx(font, element.label.c_str(), fontSize, HudLetterSpacing);

        Vector2 textPos{
            rect.x + (rect.width - textSize.x) * 0.5F,
            rect.y + (rect.height - textSize.y) * 0.5F,
        };

        DrawTextEx(font, element.label.c_str(), textPos, fontSize, HudLetterSpacing, toRlColor(element.textColor));
    }
}
struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
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

    image.pixels.reserve(static_cast<std::size_t>(image.width * image.height * 4));
    for (int i = 0; i < image.width * image.height; ++i) {
      const std::optional<std::string> rText = nextPpmToken(input);
      const std::optional<std::string> gText = nextPpmToken(input);
      const std::optional<std::string> bText = nextPpmToken(input);
      if (!rText.has_value() || !gText.has_value() || !bText.has_value()) {
        return std::nullopt;
      }

      const auto scale = [maxValue](const int value) {
        return static_cast<unsigned char>(std::clamp(value * 255 / maxValue, 0, 255));
      };
      const unsigned char r = scale(std::stoi(*rText));
      const unsigned char g = scale(std::stoi(*gText));
      const unsigned char b = scale(std::stoi(*bText));
      const unsigned char a = (r == 0U && g == 0U && b == 0U) ? 0U : 255U;
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

float pixelsPerUnit(const Camera2DComponent& camera, const int height) {
  return static_cast<float>(height) / std::max(camera.orthographicSize * 2.0F, 1.0F);
}

float worldToScreenX(const Camera2DComponent& camera, const Vec2 cameraPosition, const int width, const int height, const float x) {
  return static_cast<float>(width) * 0.5F + (x - cameraPosition.x) * pixelsPerUnit(camera, height);
}

float worldToScreenY(const Camera2DComponent& camera, const Vec2 cameraPosition, const int width, const int height, const float y) {
  (void)width;
  return static_cast<float>(height) * 0.5F - (y - cameraPosition.y) * pixelsPerUnit(camera, height);
}

Vec2 isoTileToWorld(const Vec2 tile, const float gridWidth = 32.0F, const float gridHeight = 32.0F) {
  return Vec2{
    .x = (tile.x - tile.y) * 0.65F,
    .y = -((tile.x + tile.y) - ((gridWidth + gridHeight) * 0.5F)) * 0.32F,
  };
}

void drawIsoDiamond(const Camera2DComponent& camera, const Vec2 cameraPosition, const int width, const int height, const float x, const float y, const float cells) {
  const float centerX = worldToScreenX(camera, cameraPosition, width, height, x);
  const float centerY = worldToScreenY(camera, cameraPosition, width, height, y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfW = cells * ppu * 0.65F;
  const float halfH = cells * ppu * 0.32F;
  DrawLineV({centerX, centerY - halfH}, {centerX + halfW, centerY}, {55, 88, 78, 255});
  DrawLineV({centerX + halfW, centerY}, {centerX, centerY + halfH}, {55, 88, 78, 255});
  DrawLineV({centerX, centerY + halfH}, {centerX - halfW, centerY}, {55, 88, 78, 255});
  DrawLineV({centerX - halfW, centerY}, {centerX, centerY - halfH}, {55, 88, 78, 255});
}

void drawIsoFootprint(const Camera2DComponent& camera,
                      const Vec2 cameraPosition,
                      const int width,
                      const int height,
                      const Vec2 tile,
                      const Vec2 footprint,
                      const Vec2 gridSize) {
  const Vec2 centerTile{.x = tile.x + (footprint.x - 1.0F) * 0.5F, .y = tile.y + (footprint.y - 1.0F) * 0.5F};
  const Vec2 center = isoTileToWorld(centerTile, gridSize.x, gridSize.y);
  const float centerX = worldToScreenX(camera, cameraPosition, width, height, center.x);
  const float centerY = worldToScreenY(camera, cameraPosition, width, height, center.y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfW = std::max((footprint.x + footprint.y) * ppu * 0.33F, 12.0F);
  const float halfH = std::max((footprint.x + footprint.y) * ppu * 0.16F, 6.0F);

  const ::Color color = {42, 88, 78, 180};
  DrawLineV({centerX, centerY - halfH}, {centerX + halfW, centerY}, color);
  DrawLineV({centerX + halfW, centerY}, {centerX, centerY + halfH}, color);
  DrawLineV({centerX, centerY + halfH}, {centerX - halfW, centerY}, color);
  DrawLineV({centerX - halfW, centerY}, {centerX, centerY - halfH}, color);
}

void drawEntity(const std::unordered_map<std::string, Texture2D>& textures,
                const Camera2DComponent& camera,
                const Vec2 cameraPosition,
                const Entity& entity,
                const Vec2 isoGridSize,
                const int width,
                const int height) {
  if (entity.isoGrid.has_value()) {
    for (int y = 0; y < entity.isoGrid->height; ++y) {
      for (int x = 0; x < entity.isoGrid->width; ++x) {
        const Vec2 world = isoTileToWorld(Vec2{.x = static_cast<float>(x), .y = static_cast<float>(y)},
                                          static_cast<float>(entity.isoGrid->width),
                                          static_cast<float>(entity.isoGrid->height));
        drawIsoDiamond(camera, cameraPosition, width, height, world.x, world.y, 0.5F);
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
    drawIsoFootprint(camera, cameraPosition, width, height, entity.isoTransform->tile, entity.isoTransform->footprint, isoGridSize);
  }

  const float screenX = worldToScreenX(camera, cameraPosition, width, height, position.x);
  const float screenY = worldToScreenY(camera, cameraPosition, width, height, position.y);
  ::Rectangle rect{
    .x = screenX - entityWidth * 0.5F,
    .y = screenY - entityHeight * 0.5F,
    .width = entityWidth,
    .height = entityHeight,
  };
  if (entity.buildable.has_value() && entity.isoTransform.has_value()) {
    rect.y = screenY - entityHeight + std::max((size.x + size.y) * ppu * 0.10F, 4.0F);
  }

  ::Color fillColor = {222, 166, 82, 255};
  if (entity.sprite.has_value()) {
    const auto texture = textures.find(entity.sprite->texture);
    if (texture != textures.end()) {
      ::Rectangle source{.x = 0, .y = 0, .width = static_cast<float>(texture->second.width), .height = static_cast<float>(texture->second.height)};
      DrawTexturePro(texture->second, source, rect, {0, 0}, 0.0F, WhiteTint);
      return;
    }
    fillColor = {92, 172, 238, 255};
  } else if (entity.buildable.has_value()) {
    const auto texture = textures.find(entity.buildable->asset);
    if (texture != textures.end()) {
      ::Rectangle source{.x = 0, .y = 0, .width = static_cast<float>(texture->second.width), .height = static_cast<float>(texture->second.height)};
      DrawTexturePro(texture->second, source, rect, {0, 0}, 0.0F, WhiteTint);
      return;
    }
    fillColor = {186, 128, 55, 255};
  } else if (entity.rigidbody2D.has_value() && entity.rigidbody2D->bodyType == "static") {
    fillColor = {96, 136, 96, 255};
  }
  DrawRectangleRec(rect, fillColor);

  const ::Color outlineColor = (entity.hitboxController.has_value() || entity.boxCollider2D.has_value()) ? ::Color{244, 91, 105, 255} : ::Color{245, 245, 245, 255};
  DrawRectangleLinesEx(rect, 1.0F, outlineColor);
}


Renderer2D::~Renderer2D() {
  for (auto& [id, texture] : textures_) {
    (void)id;
    UnloadTexture(texture);
  }
}

void Renderer2D::loadTextureAssets(const AssetRegistry& registry) {
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "Texture2D") {
      continue;
    }

    const std::optional<ImageData> image = loadPpm(asset.sourcePath);
    if (!image.has_value()) {
      std::cerr << "Texture load failed for " << asset.id << " from " << asset.sourcePath.string() << ". Using fallback rectangle.\n";
      continue;
    }

    ::Image rlImage{
      .data = const_cast<void*>(static_cast<const void*>(image->pixels.data())),
      .width = image->width,
      .height = image->height,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(rlImage);
    if (texture.id == 0) {
      std::cerr << "LoadTextureFromImage failed for " << asset.id << ". Using fallback rectangle.\n";
      continue;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    textures_[asset.id] = texture;
  }
}

void Renderer2D::beginFrame(const Camera2DComponent& camera, const Vec2 cameraPosition, const int width, const int height) {
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

  BeginDrawing();
  ClearBackground(toRlColor(camera.clearColor));
}

void Renderer2D::drawWorld(const World& world) {
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
    drawEntity(textures_, camera_, cameraPosition_, *entity, isoGridSize, width_, height_);
  }

  for (const DebugLine& line : world.debugLines) {
    DrawLineV(
      {worldToScreenX(camera_, cameraPosition_, width_, height_, line.start.x), worldToScreenY(camera_, cameraPosition_, width_, height_, line.start.y)},
      {worldToScreenX(camera_, cameraPosition_, width_, height_, line.end.x), worldToScreenY(camera_, cameraPosition_, width_, height_, line.end.y)},
      toRlColor(line.color));
  }
}

void Renderer2D::drawHud(const World& world) {
    const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
    const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);

    const float scaleX = static_cast<float>(width_) / canvasWidth;
    const float scaleY = static_cast<float>(height_) / canvasHeight;

    for (const HudRectElement& element : world.hudRects) {
        drawHudRect(element, scaleX, scaleY);
    }

    for (const HudButtonElement& element : world.hudButtons) {
        drawHudButton(element, scaleX, scaleY);
    }

    for (const HudTextElement& element : world.hudText) {
        drawText(element, scaleX, scaleY);
    }
}

void Renderer2D::endFrame() {
  EndDrawing();
}

} 