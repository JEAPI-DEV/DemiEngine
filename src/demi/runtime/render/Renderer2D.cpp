#include "demi/runtime/render/Renderer2D.h"
#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/ProfilerHudRenderer.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

#if DEMI_HAS_RSVG
// clang-format off: librsvg requires its primary header before rsvg-cairo.h.
#include <cairo.h>
#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>
// clang-format on
#endif

namespace demi::runtime {

namespace {

::Color toRlColor(const Color &value) {
  return ::Color{
      static_cast<unsigned char>(
          std::round(std::clamp(value.r, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.g, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.b, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.a, 0.0F, 1.0F) * 255.0F)),
  };
}

const ::Color WhiteTint = {255, 255, 255, 255};

std::optional<Texture2D> loadSvgTexture(const std::filesystem::path &path,
                                        const bool monochrome) {
#if DEMI_HAS_RSVG
  constexpr int IconRasterSize = 256;
  GError *error = nullptr;
  RsvgHandle *handle = rsvg_handle_new_from_file(path.string().c_str(), &error);
  if (handle == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return std::nullopt;
  }

  cairo_surface_t *surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, IconRasterSize, IconRasterSize);
  cairo_t *context = cairo_create(surface);
  cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(context);
  cairo_set_operator(context, CAIRO_OPERATOR_OVER);
  const RsvgRectangle viewport{.x = 0.0,
                               .y = 0.0,
                               .width = static_cast<double>(IconRasterSize),
                               .height = static_cast<double>(IconRasterSize)};
  const gboolean rendered =
      rsvg_handle_render_document(handle, context, &viewport, &error);
  cairo_surface_flush(surface);

  std::vector<unsigned char> pixels(
      static_cast<std::size_t>(IconRasterSize * IconRasterSize * 4));
  if (rendered) {
    const unsigned char *source = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < IconRasterSize; ++y) {
      for (int x = 0; x < IconRasterSize; ++x) {
        const unsigned char *bgra = source + y * stride + x * 4;
        unsigned char *rgba = pixels.data() + (y * IconRasterSize + x) * 4;
        const unsigned char alpha = bgra[3];
        const auto colorChannel = [alpha](const unsigned char value) {
          if (alpha == 0)
            return static_cast<unsigned char>(0);
          return static_cast<unsigned char>(std::min(
              255, (static_cast<int>(value) * 255 + alpha / 2) / alpha));
        };
        rgba[0] = monochrome ? 255 : colorChannel(bgra[2]);
        rgba[1] = monochrome ? 255 : colorChannel(bgra[1]);
        rgba[2] = monochrome ? 255 : colorChannel(bgra[0]);
        rgba[3] = alpha;
      }
    }
  }

  if (error != nullptr) {
    g_error_free(error);
  }
  cairo_destroy(context);
  cairo_surface_destroy(surface);
  g_object_unref(handle);
  if (!rendered) {
    return std::nullopt;
  }

  Image image{.data = pixels.data(),
              .width = IconRasterSize,
              .height = IconRasterSize,
              .mipmaps = 1,
              .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  Texture2D texture = LoadTextureFromImage(image);
  return texture.id == 0 ? std::nullopt : std::make_optional(texture);
#else
  (void)path;
  (void)monochrome;
  return std::nullopt;
#endif
}

void skipGifSubBlocks(const std::vector<unsigned char> &bytes,
                      std::size_t &cursor) {
  while (cursor < bytes.size()) {
    const std::size_t size = bytes[cursor++];
    if (size == 0 || cursor + size > bytes.size()) {
      return;
    }
    cursor += size;
  }
}

std::vector<float> loadGifFrameDurations(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
  if (bytes.size() < 13 || bytes[0] != 'G' || bytes[1] != 'I' ||
      bytes[2] != 'F') {
    return {};
  }

  std::size_t cursor = 13;
  if ((bytes[10] & 0x80U) != 0U) {
    cursor += 3U * (1U << ((bytes[10] & 0x07U) + 1U));
  }

  std::vector<float> durations;
  int pendingDelayCentiseconds = 10;
  while (cursor < bytes.size()) {
    const unsigned char block = bytes[cursor++];
    if (block == 0x3BU) {
      break;
    }
    if (block == 0x21U) {
      if (cursor >= bytes.size()) {
        break;
      }
      const unsigned char label = bytes[cursor++];
      if (label == 0xF9U && cursor < bytes.size()) {
        const std::size_t blockSize = bytes[cursor++];
        if (blockSize >= 4 && cursor + blockSize <= bytes.size()) {
          pendingDelayCentiseconds = static_cast<int>(bytes[cursor + 1]) |
                                     (static_cast<int>(bytes[cursor + 2]) << 8);
        }
        cursor += blockSize;
        if (cursor < bytes.size() && bytes[cursor] == 0U) {
          ++cursor;
        }
      } else {
        skipGifSubBlocks(bytes, cursor);
      }
      continue;
    }
    if (block != 0x2CU || cursor + 9 > bytes.size()) {
      break;
    }

    const unsigned char packed = bytes[cursor + 8];
    cursor += 9;
    if ((packed & 0x80U) != 0U) {
      cursor += 3U * (1U << ((packed & 0x07U) + 1U));
    }
    if (cursor >= bytes.size()) {
      break;
    }
    ++cursor; // LZW minimum code size.
    skipGifSubBlocks(bytes, cursor);
    durations.push_back(std::max(pendingDelayCentiseconds, 1) / 100.0F);
    pendingDelayCentiseconds = 10;
  }
  return durations;
}

int gifFrameAt(const GifAnimationTextureData &animation, const float elapsed) {
  float duration = 0.0F;
  for (const float frameDuration : animation.frameDurations) {
    duration += frameDuration;
  }
  if (duration <= 0.0F) {
    return 0;
  }

  float frameTime = std::fmod(elapsed, duration);
  for (std::size_t frame = 0; frame < animation.frameDurations.size();
       ++frame) {
    if (frameTime < animation.frameDurations[frame]) {
      return static_cast<int>(frame);
    }
    frameTime -= animation.frameDurations[frame];
  }
  return 0;
}
} // namespace

void drawText(const HudTextElement &element, float scaleX, float scaleY) {
  constexpr float HudFontBaseSize = 8.0F;
  constexpr float HudFontMinSize = 4.0F;
  constexpr float HudLetterSpacing = 5.0F;
  if (!element.visible || element.text.empty())
    return;
  const float authoredFontSize = element.fontSize > 0.0F
                                     ? element.fontSize
                                     : element.scale * HudFontBaseSize;
  const float fontSize =
      std::max(authoredFontSize * std::min(scaleX, scaleY), HudFontMinSize);
  Vector2 pos{element.position.x * scaleX, element.position.y * scaleY};
  DrawTextEx(GetFontDefault(), element.text.c_str(), pos, fontSize,
             HudLetterSpacing, toRlColor(element.color));
}
void drawHudRect(const HudRectElement &element, float scaleX, float scaleY) {
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

void drawHudPanel(const HudPanelElement &element, float scaleX, float scaleY) {
  if (!element.visible)
    return;
  const Rectangle rect{
      .x = element.position.x * scaleX,
      .y = element.position.y * scaleY,
      .width = element.size.x * scaleX,
      .height = element.size.y * scaleY,
  };
  const float radius = element.cornerRadius * std::min(scaleX, scaleY);
  if (radius <= 0.0F) {
    DrawRectangleRec(rect, toRlColor(element.color));
    if (element.borderWidth > 0.0F)
      DrawRectangleLinesEx(rect, element.borderWidth * std::min(scaleX, scaleY),
                           toRlColor(element.borderColor));
    return;
  }
  const float roundness = std::min(
      radius / std::max(std::min(rect.width, rect.height) * 0.5F, 1.0F), 1.0F);
  DrawRectangleRounded(rect, roundness, 8, toRlColor(element.color));
  if (element.borderWidth > 0.0F)
    DrawRectangleRoundedLinesEx(rect, roundness, 8,
                                element.borderWidth * std::min(scaleX, scaleY),
                                toRlColor(element.borderColor));
}

void drawHudCircle(const HudCircleElement &element, float scaleX,
                   float scaleY) {
  if (!element.visible)
    return;
  const float scale = std::min(scaleX, scaleY);
  DrawCircleV(Vector2{element.center.x * scaleX, element.center.y * scaleY},
              element.radius * scale, toRlColor(element.color));
}

void drawHudImage(
    const HudImageElement &element,
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, ImageAnimationTextureData>
        &imageAnimations,
    const std::unordered_map<std::string, GifAnimationTextureData>
        &gifAnimations,
    const float animationTime, float scaleX, float scaleY) {
  if (!element.visible || element.texture.empty()) {
    return;
  }

  std::string textureId = element.texture;
  if (!element.animation.empty()) {
    const auto animation = imageAnimations.find(element.animation);
    if (animation == imageAnimations.end() ||
        animation->second.frameCount <= 0) {
      return;
    }
    textureId =
        element.animation + "#" +
        std::to_string(element.animationFrame % animation->second.frameCount);
  } else if (const auto animation = gifAnimations.find(element.texture);
             animation != gifAnimations.end()) {
    if (animation->second.frameDurations.empty()) {
      return;
    }
    const int frame = gifFrameAt(animation->second, animationTime);
    textureId = element.texture + "#" + std::to_string(frame);
  }

  const auto texture = textures.find(textureId);
  if (texture == textures.end()) {
    return;
  }

  Rectangle source{
      .x = element.sourcePosition.x,
      .y = element.sourcePosition.y,
      .width = element.sourceSize.x != 0.0F
                   ? element.sourceSize.x
                   : static_cast<float>(texture->second.width),
      .height = element.sourceSize.y != 0.0F
                    ? element.sourceSize.y
                    : static_cast<float>(texture->second.height),
  };
  Rectangle dest{
      .x = element.position.x * scaleX,
      .y = element.position.y * scaleY,
      .width = element.size.x * scaleX,
      .height = element.size.y * scaleY,
  };

  DrawTexturePro(texture->second, source, dest, Vector2{0.0F, 0.0F}, 0.0F,
                 toRlColor(element.color));
}

void drawHudButton(const HudButtonElement &element, float scaleX,
                   float scaleY) {
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
      rect, toRlColor(element.hovered ? element.hoverColor : element.color));

  if (!element.label.empty()) {
    const float textScale = std::min(scaleX, scaleY);
    const float authoredFontSize = element.fontSize > 0.0F
                                       ? element.fontSize
                                       : element.scale * HudFontBaseSize;
    const float fontSize =
        std::max(authoredFontSize * textScale, HudFontMinSize);

    Font font = GetFontDefault();
    Vector2 textSize =
        MeasureTextEx(font, element.label.c_str(), fontSize, HudLetterSpacing);

    Vector2 textPos{
        rect.x + (rect.width - textSize.x) * 0.5F,
        rect.y + (rect.height - textSize.y) * 0.5F,
    };

    DrawTextEx(font, element.label.c_str(), textPos, fontSize, HudLetterSpacing,
               toRlColor(element.textColor));
  }
}
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

float pixelsPerUnit(const Camera2DComponent &camera, const int height) {
  return static_cast<float>(height) /
         std::max(camera.orthographicSize * 2.0F, 1.0F);
}

float worldToScreenX(const Camera2DComponent &camera, const Vec2 cameraPosition,
                     const int width, const int height, const float x) {
  return static_cast<float>(width) * 0.5F +
         (x - cameraPosition.x) * pixelsPerUnit(camera, height);
}

float worldToScreenY(const Camera2DComponent &camera, const Vec2 cameraPosition,
                     const int width, const int height, const float y) {
  (void)width;
  return static_cast<float>(height) * 0.5F -
         (y - cameraPosition.y) * pixelsPerUnit(camera, height);
}

void drawIsoDiamond(const Camera2DComponent &camera, const Vec2 cameraPosition,
                    const int width, const int height, const float x,
                    const float y, const isometric::GridDefinition &grid,
                    const ::Color color = {55, 88, 78, 255}) {
  const float centerX =
      worldToScreenX(camera, cameraPosition, width, height, x);
  const float centerY =
      worldToScreenY(camera, cameraPosition, width, height, y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfW = ppu * grid.cellWidth;
  const float halfH = ppu * grid.cellHeight;
  DrawLineV({centerX, centerY - halfH}, {centerX + halfW, centerY}, color);
  DrawLineV({centerX + halfW, centerY}, {centerX, centerY + halfH}, color);
  DrawLineV({centerX, centerY + halfH}, {centerX - halfW, centerY}, color);
  DrawLineV({centerX - halfW, centerY}, {centerX, centerY - halfH}, color);
}

void drawIsoTileTexture(const Texture2D &texture,
                        const Camera2DComponent &camera,
                        const Vec2 cameraPosition, const int width,
                        const int height, const Vec2 world,
                        const isometric::GridDefinition &grid) {
  const float centerX =
      worldToScreenX(camera, cameraPosition, width, height, world.x);
  const float centerY =
      worldToScreenY(camera, cameraPosition, width, height, world.y);
  const float ppu = pixelsPerUnit(camera, height);
  const float halfWidth = ppu * grid.cellWidth;
  const float halfHeight = ppu * grid.cellHeight;
  const ::Rectangle source{.x = 0.0F,
                           .y = 0.0F,
                           .width = static_cast<float>(texture.width),
                           .height = static_cast<float>(texture.height)};
  const ::Rectangle destination{.x = centerX - halfWidth,
                                .y = centerY - halfHeight,
                                .width = halfWidth * 2.0F,
                                .height = halfHeight * 2.0F};
  DrawTexturePro(texture, source, destination, {}, 0.0F, WhiteTint);
}

void drawIsoFootprint(const Camera2DComponent &camera,
                      const Vec2 cameraPosition, const int width,
                      const int height, const Vec2 tile, const Vec2 footprint,
                      const isometric::GridDefinition &grid,
                      const ::Color color = {42, 88, 78, 180}) {
  const int footprintWidth = std::max(static_cast<int>(footprint.x), 1);
  const int footprintHeight = std::max(static_cast<int>(footprint.y), 1);
  for (int y = 0; y < footprintHeight; ++y) {
    for (int x = 0; x < footprintWidth; ++x) {
      const Vec2 cell{.x = tile.x + static_cast<float>(x),
                      .y = tile.y + static_cast<float>(y)};
      const Vec2 world = isometric::tileToWorld(grid, cell);
      drawIsoDiamond(camera, cameraPosition, width, height, world.x, world.y,
                     grid, color);
    }
  }
}

void drawTilemap(
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, TilemapAsset2D> &tilemaps,
    const World &world, const Camera2DComponent &camera,
    const Vec2 cameraPosition, const Entity &entity, const int width,
    const int height) {
  const auto *component = entity.component<Tilemap2DComponent>();
  if (component == nullptr)
    return;
  const auto foundAsset = tilemaps.find(component->asset);
  if (foundAsset == tilemaps.end())
    return;
  const TilemapAsset2D &asset = foundAsset->second;
  const auto foundTexture = textures.find(asset.texture);
  if (foundTexture == textures.end())
    return;
  const Texture2D &texture = foundTexture->second;
  const Vec2 origin = worldPosition2D(world, entity);
  const float tileWorldWidth =
      static_cast<float>(asset.tileWidth) / component->pixelsPerUnit;
  const float tileWorldHeight =
      static_cast<float>(asset.tileHeight) / component->pixelsPerUnit;
  const int atlasColumns = std::max(texture.width / asset.tileWidth, 1);
  const float ppu = pixelsPerUnit(camera, height);

  for (const TilemapLayer2D &layer : asset.layers) {
    const Vec2 layerCamera{cameraPosition.x * layer.parallax,
                           cameraPosition.y * layer.parallax};
    for (int row = 0; row < asset.rows; ++row) {
      for (int column = 0; column < asset.columns; ++column) {
        const int tile =
            layer.tiles[static_cast<std::size_t>(row * asset.columns + column)];
        if (tile <= 0)
          continue;
        const int atlasIndex = tile - 1;
        const ::Rectangle source{
            .x =
                static_cast<float>(atlasIndex % atlasColumns * asset.tileWidth),
            .y = static_cast<float>(atlasIndex / atlasColumns *
                                    asset.tileHeight),
            .width = static_cast<float>(asset.tileWidth),
            .height = static_cast<float>(asset.tileHeight)};
        const Vec2 center{
            origin.x + (static_cast<float>(column) + 0.5F) * tileWorldWidth,
            origin.y + (static_cast<float>(asset.rows - row) - 0.5F) *
                           tileWorldHeight};
        const float screenX =
            worldToScreenX(camera, layerCamera, width, height, center.x);
        const float screenY =
            worldToScreenY(camera, layerCamera, width, height, center.y);
        const float drawWidth = tileWorldWidth * ppu;
        const float drawHeight = tileWorldHeight * ppu;
        if (screenX + drawWidth * 0.5F < 0.0F ||
            screenX - drawWidth * 0.5F > static_cast<float>(width) ||
            screenY + drawHeight * 0.5F < 0.0F ||
            screenY - drawHeight * 0.5F > static_cast<float>(height))
          continue;
        const ::Rectangle destination{.x = screenX - drawWidth * 0.5F,
                                      .y = screenY - drawHeight * 0.5F,
                                      .width = drawWidth,
                                      .height = drawHeight};
        DrawTexturePro(texture, source, destination, {}, 0.0F,
                       ::Color{255, 255, 255,
                               static_cast<unsigned char>(
                                   std::round(layer.opacity * 255.0F))});
      }
    }
  }
}

void drawEntity(const std::unordered_map<std::string, Texture2D> &textures,
                const std::unordered_map<std::string, TilemapAsset2D> &tilemaps,
                const World &world, const Camera2DComponent &camera,
                const Vec2 cameraPosition, const Entity &entity,
                const isometric::GridDefinition &isoGrid, const int width,
                const int height) {
  if (entity.hasComponent<Tilemap2DComponent>()) {
    drawTilemap(textures, tilemaps, world, camera, cameraPosition, entity,
                width, height);
    return;
  }
  if (entity.hasComponent<IsoGridComponent>()) {
    const auto &component = *entity.component<IsoGridComponent>();
    for (int y = 0; y < component.height; ++y) {
      for (int x = 0; x < component.width; ++x) {
        const Vec2 world =
            isometric::tileToWorld(isoGrid, Vec2{.x = static_cast<float>(x),
                                                 .y = static_cast<float>(y)});
        const std::string cellKey = std::to_string(x) + "," + std::to_string(y);
        const auto authoredTexture = component.cellTextures.find(cellKey);
        const std::string &textureId =
            authoredTexture != component.cellTextures.end()
                ? authoredTexture->second
                : component.defaultTexture;
        if (const auto texture = textures.find(textureId);
            texture != textures.end()) {
          drawIsoTileTexture(texture->second, camera, cameraPosition, width,
                             height, world, isoGrid);
        }
        drawIsoDiamond(camera, cameraPosition, width, height, world.x, world.y,
                       isoGrid, {70, 112, 92, 190});
      }
    }
    return;
  }

  Vec2 position;
  Vec2 size = {1.0F, 1.0F};
  if (entity.hasComponent<Transform2DComponent>()) {
    position = worldPosition2D(world, entity);
  }
  if (entity.hasComponent<IsoTransformComponent>()) {
    const auto &transform = *entity.component<IsoTransformComponent>();
    size = transform.footprint;
    Vec2 renderTile = transform.tile;
    if (entity.hasComponent<BuildableComponent>()) {
      renderTile.x += (size.x - 1.0F) * 0.5F;
      renderTile.y += (size.y - 1.0F) * 0.5F;
    }
    position = isometric::tileToWorld(isoGrid, renderTile);
    position.y += transform.height;
  }
  if (entity.hasComponent<HitboxControllerComponent>()) {
    size = entity.component<HitboxControllerComponent>()->hurtbox;
  }
  if (entity.hasComponent<BoxCollider2DComponent>()) {
    size = entity.component<BoxCollider2DComponent>()->size;
    if (entity.hasComponent<Transform2DComponent>()) {
      position.x += entity.component<BoxCollider2DComponent>()->offset.x;
      position.y += entity.component<BoxCollider2DComponent>()->offset.y;
    }
  }
  if (entity.hasComponent<CircleCollider2DComponent>()) {
    const auto &circle = *entity.component<CircleCollider2DComponent>();
    size = {circle.radius * 2.0F, circle.radius * 2.0F};
    if (entity.hasComponent<Transform2DComponent>()) {
      position.x += circle.offset.x;
      position.y += circle.offset.y;
    }
  }

  const float ppu = pixelsPerUnit(camera, height);
  float entityWidth = std::max(size.x * ppu, 24.0F);
  float entityHeight = std::max(size.y * ppu, 24.0F);
  if (entity.hasComponent<BuildableComponent>() &&
      entity.hasComponent<IsoTransformComponent>()) {
    entityWidth = std::max((size.x + size.y) * ppu * 0.62F, 22.0F);
    entityHeight =
        std::max((size.x + size.y) * ppu * isoGrid.cellHeight * 0.95F, 22.0F);
  }

  const float screenX =
      worldToScreenX(camera, cameraPosition, width, height, position.x);
  const float screenY =
      worldToScreenY(camera, cameraPosition, width, height, position.y);
  ::Rectangle rect{
      .x = screenX - entityWidth * 0.5F,
      .y = screenY - entityHeight * 0.5F,
      .width = entityWidth,
      .height = entityHeight,
  };
  if (entity.hasComponent<BuildableComponent>() &&
      entity.hasComponent<IsoTransformComponent>()) {
    const float footprintBottom =
        (size.x + size.y) * isoGrid.cellHeight * ppu * 0.5F;
    rect.y = screenY + footprintBottom - entityHeight;
  }

  ::Color fillColor = {222, 166, 82, 255};
  if (entity.hasComponent<SpriteComponent>()) {
    const SpriteComponent &sprite = *entity.component<SpriteComponent>();
    const auto texture = textures.find(sprite.texture);
    if (texture != textures.end()) {
      float sourceX = sprite.sourcePosition.x;
      float sourceY = sprite.sourcePosition.y;
      float sourceWidth = sprite.sourceSize.x > 0.0F
                              ? sprite.sourceSize.x
                              : static_cast<float>(texture->second.width);
      float sourceHeight = sprite.sourceSize.y > 0.0F
                               ? sprite.sourceSize.y
                               : static_cast<float>(texture->second.height);
      if (const auto *animator = entity.component<SpriteAnimator2DComponent>();
          animator != nullptr && animator->frameSize.x > 0.0F &&
          animator->frameSize.y > 0.0F) {
        sourceWidth = animator->frameSize.x;
        sourceHeight = animator->frameSize.y;
        const int columns =
            std::max(static_cast<int>(texture->second.width / sourceWidth), 1);
        sourceX =
            static_cast<float>(animator->currentFrame % columns) * sourceWidth;
        sourceY =
            static_cast<float>(animator->currentFrame / columns) * sourceHeight;
      }
      ::Rectangle source{.x = sourceX,
                         .y = sourceY,
                         .width = sprite.flipX ? -sourceWidth : sourceWidth,
                         .height = sprite.flipY ? -sourceHeight : sourceHeight};
      ::Rectangle destination{.x = screenX,
                              .y = screenY,
                              .width = rect.width,
                              .height = rect.height};
      const ::Vector2 origin{sprite.pivot.x * rect.width,
                             sprite.pivot.y * rect.height};
      const float rotation = worldRotation2D(world, entity) * RAD2DEG;
      DrawTexturePro(texture->second, source, destination, origin, rotation,
                     toRlColor(sprite.color));
      return;
    }
    fillColor = toRlColor(sprite.color);
    if (sprite.shape == "circle") {
      DrawEllipse(static_cast<int>(screenX), static_cast<int>(screenY),
                  entityWidth * 0.5F, entityHeight * 0.5F, fillColor);
      DrawEllipseLines(static_cast<int>(screenX), static_cast<int>(screenY),
                       entityWidth * 0.5F, entityHeight * 0.5F,
                       {245, 245, 245, 255});
      return;
    }
    if (sprite.shape == "triangle") {
      const ::Vector2 top{screenX, rect.y};
      const ::Vector2 right{rect.x + rect.width, rect.y + rect.height};
      const ::Vector2 left{rect.x, rect.y + rect.height};
      DrawTriangle(top, left, right, fillColor);
      DrawLineV(top, right, {245, 245, 245, 255});
      DrawLineV(right, left, {245, 245, 245, 255});
      DrawLineV(left, top, {245, 245, 245, 255});
      return;
    }
  } else if (entity.hasComponent<BuildableComponent>()) {
    const auto texture =
        textures.find(entity.component<BuildableComponent>()->asset);
    if (texture != textures.end()) {
      ::Rectangle source{.x = 0,
                         .y = 0,
                         .width = static_cast<float>(texture->second.width),
                         .height = static_cast<float>(texture->second.height)};
      DrawTexturePro(texture->second, source, rect, {0, 0}, 0.0F, WhiteTint);
      return;
    }
    fillColor = {186, 128, 55, 255};
  } else if (entity.hasComponent<CircleCollider2DComponent>()) {
    DrawEllipse(static_cast<int>(screenX), static_cast<int>(screenY),
                entityWidth * 0.5F, entityHeight * 0.5F, {186, 128, 55, 255});
    DrawEllipseLines(static_cast<int>(screenX), static_cast<int>(screenY),
                     entityWidth * 0.5F, entityHeight * 0.5F,
                     {244, 91, 105, 255});
    return;
  } else if (entity.hasComponent<Rigidbody2DComponent>() &&
             entity.component<Rigidbody2DComponent>()->bodyType == "static") {
    fillColor = {96, 136, 96, 255};
  }
  DrawRectangleRec(rect, fillColor);

  const ::Color outlineColor =
      (entity.hasComponent<HitboxControllerComponent>() ||
       entity.hasComponent<BoxCollider2DComponent>())
          ? ::Color{244, 91, 105, 255}
          : ::Color{245, 245, 245, 255};
  DrawRectangleLinesEx(rect, 1.0F, outlineColor);
}

Renderer2D::~Renderer2D() {
  for (auto &[id, texture] : textures_) {
    (void)id;
    UnloadTexture(texture);
  }
}

void Renderer2D::loadTextureAssets(const AssetRegistry &registry) {
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type == "Tilemap2D") {
      std::string error;
      if (auto tilemap = loadTilemapAsset(asset, error)) {
        tilemaps_[asset.id] = std::move(*tilemap);
      } else {
        std::cerr << "Tilemap load failed for " << asset.id << ": " << error
                  << ".\n";
      }
      continue;
    }
    if (asset.type == "GifAnimation2D") {
      int frameCount = 0;
      Image frames =
          LoadImageAnim(asset.sourcePath.string().c_str(), &frameCount);
      if (frames.data == nullptr || frameCount <= 0) {
        std::cerr << "GIF load failed for " << asset.id << " from "
                  << asset.sourcePath.string() << ".\n";
        continue;
      }
      const int frameBytes =
          GetPixelDataSize(frames.width, frames.height, frames.format);
      for (int frame = 0; frame < frameCount; ++frame) {
        Image image{.data = static_cast<unsigned char *>(frames.data) +
                            frameBytes * frame,
                    .width = frames.width,
                    .height = frames.height,
                    .mipmaps = 1,
                    .format = frames.format};
        Texture2D texture = LoadTextureFromImage(image);
        if (texture.id != 0) {
          textures_[asset.id + "#" + std::to_string(frame)] = texture;
        }
      }
      UnloadImage(frames);
      std::vector<float> frameDurations =
          loadGifFrameDurations(asset.sourcePath);
      if (frameDurations.size() != static_cast<std::size_t>(frameCount)) {
        frameDurations.assign(static_cast<std::size_t>(frameCount), 0.1F);
      }
      gifAnimations_[asset.id] =
          GifAnimationTextureData{.frameDurations = std::move(frameDurations)};
      continue;
    }
    if (asset.type == "Icon2D") {
      const std::optional<Texture2D> texture =
          loadSvgTexture(asset.sourcePath, true);
      if (!texture.has_value()) {
        std::cerr << "Icon load failed for " << asset.id << " from "
                  << asset.sourcePath.string() << ".\n";
        continue;
      }
      SetTextureFilter(*texture, TEXTURE_FILTER_BILINEAR);
      textures_[asset.id] = *texture;
      continue;
    }
    if (asset.type == "SvgTexture2D") {
      const std::optional<Texture2D> texture =
          loadSvgTexture(asset.sourcePath, false);
      if (!texture.has_value()) {
        std::cerr << "SVG texture load failed for " << asset.id << " from "
                  << asset.sourcePath.string() << ".\n";
        continue;
      }
      SetTextureFilter(*texture, TEXTURE_FILTER_BILINEAR);
      textures_[asset.id] = *texture;
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
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        textures_[asset.id + "#" + std::to_string(frame)] = texture;
      }
      if (loaded) {
        imageAnimations_[asset.id] = ImageAnimationTextureData{
            .frameCount = static_cast<int>(asset.sourcePaths.size())};
      }
      continue;
    }
    if (asset.type != "Texture2D") {
      continue;
    }

    const std::optional<ImageData> image = loadPpm(asset.sourcePath);
    if (!image.has_value()) {
      std::cerr << "Texture load failed for " << asset.id << " from "
                << asset.sourcePath.string() << ". Using fallback rectangle.\n";
      continue;
    }

    ::Image rlImage{
        .data =
            const_cast<void *>(static_cast<const void *>(image->pixels.data())),
        .width = image->width,
        .height = image->height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(rlImage);
    if (texture.id == 0) {
      std::cerr << "LoadTextureFromImage failed for " << asset.id
                << ". Using fallback rectangle.\n";
      continue;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    textures_[asset.id] = texture;
  }
}

void Renderer2D::beginFrame(const Camera2DComponent &camera,
                            const Vec2 cameraPosition, const int width,
                            const int height) {
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);
  animationTime_ += GetFrameTime();

  BeginDrawing();
  ClearBackground(toRlColor(camera.clearColor));
}

void Renderer2D::drawWorld(const World &world) {
  if (world.debug.grid) {
    const float ppu = pixelsPerUnit(camera_, height_);
    const int halfColumns = static_cast<int>(width_ / ppu * 0.5F) + 2;
    const int halfRows = static_cast<int>(height_ / ppu * 0.5F) + 2;
    const int centerX = static_cast<int>(std::floor(cameraPosition_.x));
    const int centerY = static_cast<int>(std::floor(cameraPosition_.y));
    for (int x = centerX - halfColumns; x <= centerX + halfColumns; ++x) {
      const float screenX = worldToScreenX(camera_, cameraPosition_, width_,
                                           height_, static_cast<float>(x));
      DrawLine(static_cast<int>(screenX), 0, static_cast<int>(screenX), height_,
               {70, 90, 110, 90});
    }
    for (int y = centerY - halfRows; y <= centerY + halfRows; ++y) {
      const float screenY = worldToScreenY(camera_, cameraPosition_, width_,
                                           height_, static_cast<float>(y));
      DrawLine(0, static_cast<int>(screenY), width_, static_cast<int>(screenY),
               {70, 90, 110, 90});
    }
  }
  const isometric::GridDefinition isoGrid =
      isometric::gridDefinition(world).value_or(
          isometric::GridDefinition{.width = 32, .height = 32});

  std::vector<const Entity *> renderables;
  for (const Entity &entity : world.entities) {
    if (entity.hasComponent<SpriteComponent>() ||
        entity.hasComponent<Tilemap2DComponent>() ||
        entity.hasComponent<HitboxControllerComponent>() ||
        entity.hasComponent<IsoGridComponent>() ||
        entity.hasComponent<BuildableComponent>() ||
        (entity.hasComponent<BoxCollider2DComponent>() &&
         entity.component<BoxCollider2DComponent>()->debugVisible) ||
        (entity.hasComponent<CircleCollider2DComponent>() &&
         entity.component<CircleCollider2DComponent>()->debugVisible)) {
      renderables.push_back(&entity);
    }
  }

  std::ranges::stable_sort(
      renderables, [](const Entity *left, const Entity *right) {
        const auto sortKey = [](const Entity *entity) {
          std::string_view layer;
          int order = 0;
          if (const auto *sprite = entity->component<SpriteComponent>()) {
            layer = sprite->layer;
            order = sprite->sortingOrder;
          } else if (const auto *tilemap =
                         entity->component<Tilemap2DComponent>()) {
            layer = tilemap->layer;
            order = tilemap->sortingOrder;
          }
          float depth = 0.0F;
          if (entity->hasComponent<IsoGridComponent>()) {
            depth = -10000.0F;
          } else if (entity->hasComponent<IsoTransformComponent>()) {
            depth = entity->component<IsoTransformComponent>()->tile.x +
                    entity->component<IsoTransformComponent>()->tile.y +
                    entity->component<IsoTransformComponent>()->height;
          }
          return std::tuple{order, layer, depth, entity->id};
        };
        return sortKey(left) < sortKey(right);
      });

  for (const Entity *entity : renderables) {
    drawEntity(textures_, tilemaps_, world, camera_, cameraPosition_, *entity,
               isoGrid, width_, height_);
  }

  for (const Entity *entity : renderables) {
    if (entity->hasComponent<BuildableComponent>() &&
        entity->hasComponent<IsoTransformComponent>()) {
      drawIsoFootprint(camera_, cameraPosition_, width_, height_,
                       entity->component<IsoTransformComponent>()->tile,
                       entity->component<IsoTransformComponent>()->footprint,
                       isoGrid, {238, 190, 88, 220});
    }
  }

  if (world.placementPreview.visible) {
    drawIsoFootprint(camera_, cameraPosition_, width_, height_,
                     world.placementPreview.tile,
                     world.placementPreview.footprint, isoGrid,
                     world.placementPreview.valid ? ::Color{72, 220, 145, 255}
                                                  : ::Color{244, 91, 105, 255});
  }

  for (const DebugLine &line : world.debugLines) {
    DrawLineV(
        {worldToScreenX(camera_, cameraPosition_, width_, height_,
                        line.start.x),
         worldToScreenY(camera_, cameraPosition_, width_, height_,
                        line.start.y)},
        {worldToScreenX(camera_, cameraPosition_, width_, height_, line.end.x),
         worldToScreenY(camera_, cameraPosition_, width_, height_, line.end.y)},
        toRlColor(line.color));
  }

  const float ppu = pixelsPerUnit(camera_, height_);
  int drawIndex = 0;
  for (const Entity *entity : renderables) {
    const Vec2 position = worldPosition2D(world, *entity);
    const float screenX =
        worldToScreenX(camera_, cameraPosition_, width_, height_, position.x);
    const float screenY =
        worldToScreenY(camera_, cameraPosition_, width_, height_, position.y);
    if (world.debug.colliders) {
      if (const auto *box = entity->component<BoxCollider2DComponent>()) {
        const float boxX = screenX + box->offset.x * ppu;
        const float boxY = screenY - box->offset.y * ppu;
        DrawRectangleLinesEx({boxX - box->size.x * ppu * 0.5F,
                              boxY - box->size.y * ppu * 0.5F,
                              box->size.x * ppu, box->size.y * ppu},
                             2.0F, {70, 220, 255, 230});
      }
      if (const auto *circle = entity->component<CircleCollider2DComponent>()) {
        DrawCircleLines(static_cast<int>(screenX + circle->offset.x * ppu),
                        static_cast<int>(screenY - circle->offset.y * ppu),
                        circle->radius * ppu, {70, 220, 255, 230});
      }
    }
    if (world.debug.entityIds || world.debug.drawOrder) {
      std::string label;
      if (world.debug.drawOrder)
        label = "#" + std::to_string(drawIndex) + " ";
      if (world.debug.entityIds)
        label += entity->id;
      DrawText(label.c_str(), static_cast<int>(screenX + 5.0F),
               static_cast<int>(screenY - 18.0F), 14, {255, 226, 100, 255});
    }
    ++drawIndex;
  }
  if (world.debug.contacts) {
    for (const PhysicsContact2D &contact : world.physicsContacts) {
      const auto found =
          std::ranges::find_if(world.entities, [&](const Entity &entity) {
            return entity.id == contact.entityId;
          });
      if (found == world.entities.end())
        continue;
      const Vec2 position = worldPosition2D(world, *found);
      const ::Vector2 start{
          worldToScreenX(camera_, cameraPosition_, width_, height_, position.x),
          worldToScreenY(camera_, cameraPosition_, width_, height_,
                         position.y)};
      DrawLineV(start,
                {start.x + contact.normal.x * 28.0F,
                 start.y - contact.normal.y * 28.0F},
                {255, 90, 90, 255});
    }
  }
}

void Renderer2D::drawHud(const World &world) {
  const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
  const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);

  const float scaleX = static_cast<float>(width_) / canvasWidth;
  const float scaleY = static_cast<float>(height_) / canvasHeight;

  int maxLayer = 0;
  const auto findMaxLayer = [&maxLayer](const auto &elements) {
    for (const auto &element : elements)
      maxLayer = std::max(maxLayer, element.layer);
  };
  findMaxLayer(world.hudRects);
  findMaxLayer(world.hudPanels);
  findMaxLayer(world.hudCircles);
  findMaxLayer(world.hudImages);
  findMaxLayer(world.hudButtons);
  findMaxLayer(world.hudText);

  for (int layer = 0; layer <= maxLayer; ++layer) {
    for (const HudRectElement &element : world.hudRects)
      if (element.layer == layer)
        drawHudRect(element, scaleX, scaleY);
    for (const HudPanelElement &element : world.hudPanels)
      if (element.layer == layer)
        drawHudPanel(element, scaleX, scaleY);
    for (const HudCircleElement &element : world.hudCircles)
      if (element.layer == layer)
        drawHudCircle(element, scaleX, scaleY);
    for (const HudImageElement &element : world.hudImages)
      if (element.layer == layer)
        drawHudImage(element, textures_, imageAnimations_, gifAnimations_,
                     animationTime_, scaleX, scaleY);
    for (const HudButtonElement &element : world.hudButtons)
      if (element.layer == layer)
        drawHudButton(element, scaleX, scaleY);
    for (const HudTextElement &element : world.hudText)
      if (element.layer == layer)
        drawText(element, scaleX, scaleY);
  }

  if (world.debug.uiBounds) {
    const auto drawBounds = [scaleX, scaleY](const auto &elements) {
      for (const auto &element : elements) {
        if (!element.visible)
          continue;
        DrawRectangleLinesEx({element.position.x * scaleX,
                              element.position.y * scaleY,
                              element.size.x * scaleX, element.size.y * scaleY},
                             1.0F, {255, 80, 210, 230});
      }
    };
    drawBounds(world.hudRects);
    drawBounds(world.hudPanels);
    drawBounds(world.hudImages);
    drawBounds(world.hudButtons);
    for (const HudCircleElement &circle : world.hudCircles)
      if (circle.visible)
        DrawCircleLines(static_cast<int>(circle.center.x * scaleX),
                        static_cast<int>(circle.center.y * scaleY),
                        circle.radius * std::min(scaleX, scaleY),
                        {255, 80, 210, 230});
  }
  if (world.debug.profilerHud && RuntimeProfiler::enabled()) {
    drawProfilerHud(RuntimeProfiler::frameSummary(0.05), width_, height_);
  }
}

void Renderer2D::endFrame() { EndDrawing(); }

} // namespace demi::runtime
