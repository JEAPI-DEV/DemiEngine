#include "demi/assets/GeneratedAtlasCooker.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <span>

namespace demi::assets {
namespace {

constexpr unsigned DefaultPageSize = 1024;

struct Image {
  unsigned width = 0;
  unsigned height = 0;
  std::vector<unsigned char> rgba;
};

struct Placement {
  std::size_t page = 0;
  unsigned x = 0;
  unsigned y = 0;
};

void error(GeneratedAtlasCookResult &result, std::string code,
           std::string message, const std::filesystem::path &path) {
  result.diagnostics.push_back({.severity = Severity::Error,
                                .code = std::move(code),
                                .message = std::move(message),
                                .path = path.string()});
}

std::string idPath(std::string id) {
  if (id.starts_with("asset://"))
    id.erase(0, 8);
  std::ranges::replace(id, ':', '_');
  return id;
}

std::optional<nlohmann::json> readJson(const std::filesystem::path &path,
                                       GeneratedAtlasCookResult &result) {
  try {
    std::ifstream input(path);
    if (!input) {
      error(result, "ATLAS_SOURCE_NOT_FOUND",
            "Atlas descriptor could not be read.", path);
      return std::nullopt;
    }
    return nlohmann::json::parse(input);
  } catch (const nlohmann::json::exception &exception) {
    error(result, "ATLAS_SOURCE_INVALID", exception.what(), path);
    return std::nullopt;
  }
}

std::optional<Image> decodePng(const std::filesystem::path &path,
                               GeneratedAtlasCookResult &result) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    error(result, "ATLAS_SPRITE_DECODE_FAILED",
          "Atlas sprite could not be read.", path);
    return std::nullopt;
  }
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> encoded(
      static_cast<std::size_t>(std::max(size, std::streamsize{0})));
  if (size <= 0 ||
      !input.read(reinterpret_cast<char *>(encoded.data()), size)) {
    error(result, "ATLAS_SPRITE_DECODE_FAILED",
          "Atlas sprite is empty or incomplete.", path);
    return std::nullopt;
  }
  bx::DefaultAllocator allocator;
  bimg::ImageContainer *decoded = bimg::imageParse(
      &allocator, encoded.data(), static_cast<std::uint32_t>(encoded.size()),
      bimg::TextureFormat::RGBA8);
  if (decoded == nullptr || decoded->m_width == 0 || decoded->m_height == 0) {
    error(result, "ATLAS_SPRITE_DECODE_FAILED",
          "Atlas sprite must be a valid PNG.", path);
    return std::nullopt;
  }
  Image image{.width = decoded->m_width,
              .height = decoded->m_height,
              .rgba = std::vector<unsigned char>(decoded->m_size)};
  std::memcpy(image.rgba.data(), decoded->m_data, decoded->m_size);
  bimg::imageFree(decoded);
  return image;
}

bool writePng(const std::filesystem::path &path, const Image &image,
              GeneratedAtlasCookResult &result) {
  std::filesystem::create_directories(path.parent_path());
  bx::FileWriter writer;
  bx::Error writeError;
  const bool opened =
      bx::open(&writer, path.string().c_str(), false, &writeError);
  if (opened &&
      bimg::imageWritePng(&writer, image.width, image.height, image.width * 4U,
                          image.rgba.data(), bimg::TextureFormat::RGBA8, false,
                          &writeError) > 0 &&
      writeError.isOk()) {
    bx::close(&writer);
    result.outputs.push_back(path);
    return true;
  }
  if (opened)
    bx::close(&writer);
  error(result, "ATLAS_PAGE_WRITE_FAILED",
        "Could not encode generated atlas PNG.", path);
  return false;
}

bool writeMetadata(const std::filesystem::path &path,
                   const nlohmann::json &document,
                   GeneratedAtlasCookResult &result) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    error(result, "ATLAS_METADATA_WRITE_FAILED",
          "Could not write generated atlas metadata.", path);
    return false;
  }
  output << document.dump(2) << '\n';
  result.outputs.push_back(path);
  return true;
}

Placement place(unsigned width, unsigned height, unsigned pageWidth,
                unsigned pageHeight, unsigned padding,
                std::vector<std::array<unsigned, 3>> &cursors,
                GeneratedAtlasCookResult &result,
                const std::filesystem::path &path) {
  const unsigned paddedWidth = width + padding * 2U;
  const unsigned paddedHeight = height + padding * 2U;
  if (paddedWidth > pageWidth || paddedHeight > pageHeight) {
    error(result, "ATLAS_SPRITE_TOO_LARGE",
          "Atlas content exceeds the configured page dimensions.", path);
    return {};
  }
  for (std::size_t page = 0; page <= cursors.size(); ++page) {
    if (page == cursors.size())
      cursors.push_back({0U, 0U, 0U});
    auto &[x, y, rowHeight] = cursors[page];
    if (x + paddedWidth > pageWidth) {
      x = 0;
      y += rowHeight;
      rowHeight = 0;
    }
    if (y + paddedHeight > pageHeight)
      continue;
    const Placement placement{.page = page, .x = x + padding, .y = y + padding};
    x += paddedWidth;
    rowHeight = std::max(rowHeight, paddedHeight);
    return placement;
  }
  return {};
}

void blitWithBleed(Image &page, const Image &source, const Placement placement,
                   const unsigned bleed) {
  for (int y = -static_cast<int>(bleed);
       y < static_cast<int>(source.height + bleed); ++y) {
    const unsigned sourceY = static_cast<unsigned>(
        std::clamp(y, 0, static_cast<int>(source.height) - 1));
    for (int x = -static_cast<int>(bleed);
         x < static_cast<int>(source.width + bleed); ++x) {
      const unsigned sourceX = static_cast<unsigned>(
          std::clamp(x, 0, static_cast<int>(source.width) - 1));
      const unsigned targetX =
          static_cast<unsigned>(static_cast<int>(placement.x) + x);
      const unsigned targetY =
          static_cast<unsigned>(static_cast<int>(placement.y) + y);
      const std::size_t sourceOffset =
          (static_cast<std::size_t>(sourceY) * source.width + sourceX) * 4U;
      const std::size_t targetOffset =
          (static_cast<std::size_t>(targetY) * page.width + targetX) * 4U;
      std::copy_n(
          source.rgba.begin() + static_cast<std::ptrdiff_t>(sourceOffset), 4,
          page.rgba.begin() + static_cast<std::ptrdiff_t>(targetOffset));
    }
  }
}

GeneratedAtlasCookResult
cookTextureAtlas(const AssetManifest &asset, const AssetRegistry &registry,
                 const std::filesystem::path &directory) {
  GeneratedAtlasCookResult result;
  const auto descriptor = readJson(asset.sourcePath, result);
  if (!descriptor || !descriptor->is_object() ||
      descriptor->value("format_version", 0) != 1 ||
      !descriptor->contains("sprites") ||
      !(*descriptor)["sprites"].is_array() ||
      (*descriptor)["sprites"].empty()) {
    error(result, "ATLAS_DESCRIPTOR_INVALID",
          "Texture atlas descriptors require format_version 1 and sprites.",
          asset.sourcePath);
    return result;
  }
  const unsigned pageWidth = descriptor->value("page_width", DefaultPageSize);
  const unsigned pageHeight = descriptor->value("page_height", DefaultPageSize);
  const unsigned padding = descriptor->value("padding", 2U);
  const unsigned bleed = descriptor->value("bleed", 1U);
  if (pageWidth == 0 || pageHeight == 0 || pageWidth > 8192 ||
      pageHeight > 8192 || bleed > padding) {
    error(result, "ATLAS_SETTINGS_INVALID",
          "Atlas pages must be 1..8192 pixels and bleed cannot exceed padding.",
          asset.sourcePath);
    return result;
  }

  struct Sprite {
    std::string id;
    std::string source;
    nlohmann::json pivot;
    nlohmann::json border;
    std::string animationTag;
    Image image;
  };
  std::vector<Sprite> sprites;
  std::set<std::string> ids;
  for (const auto &entry : (*descriptor)["sprites"]) {
    const std::string id = entry.value("id", "");
    const std::string sourceId = entry.value("source", "");
    if (!id.starts_with("asset://") || !sourceId.starts_with("asset://") ||
        !ids.insert(id).second) {
      error(result, "ATLAS_SPRITE_INVALID",
            "Atlas sprites require unique id and source asset:// references.",
            asset.sourcePath);
      continue;
    }
    const AssetManifest *source = findAsset(registry, sourceId);
    if (source == nullptr) {
      error(result, "ATLAS_SPRITE_SOURCE_NOT_FOUND",
            "Atlas sprite source was not found: " + sourceId, asset.sourcePath);
      continue;
    }
    if (std::ranges::find(asset.dependencies, sourceId) ==
        asset.dependencies.end()) {
      error(
          result, "ATLAS_DEPENDENCY_UNDECLARED",
          "Atlas sprite sources must also be declared as asset dependencies: " +
              sourceId,
          asset.manifestPath);
      continue;
    }
    auto image = decodePng(source->sourcePath, result);
    if (!image)
      continue;
    sprites.push_back(
        {.id = id,
         .source = sourceId,
         .pivot = entry.value("pivot", nlohmann::json{0.5, 0.5}),
         .border = entry.value("border", nlohmann::json{0, 0, 0, 0}),
         .animationTag = entry.value("animation_tag", ""),
         .image = std::move(*image)});
  }
  if (hasErrors(result.diagnostics) || sprites.empty())
    return result;
  std::ranges::sort(sprites, {}, &Sprite::id);

  std::vector<std::array<unsigned, 3>> cursors;
  std::vector<Image> pages;
  nlohmann::json metadataSprites = nlohmann::json::array();
  for (const Sprite &sprite : sprites) {
    const Placement placement =
        place(sprite.image.width, sprite.image.height, pageWidth, pageHeight,
              padding, cursors, result, asset.sourcePath);
    if (hasErrors(result.diagnostics))
      return result;
    while (pages.size() <= placement.page)
      pages.push_back(
          {.width = pageWidth,
           .height = pageHeight,
           .rgba = std::vector<unsigned char>(
               static_cast<std::size_t>(pageWidth) * pageHeight * 4U)});
    blitWithBleed(pages[placement.page], sprite.image, placement, bleed);
    metadataSprites.push_back(
        {{"id", sprite.id},
         {"source", sprite.source},
         {"page", placement.page},
         {"rect",
          {placement.x, placement.y, sprite.image.width, sprite.image.height}},
         {"pivot", sprite.pivot},
         {"border", sprite.border},
         {"animation_tag", sprite.animationTag}});
  }
  nlohmann::json pageNames = nlohmann::json::array();
  for (std::size_t page = 0; page < pages.size(); ++page) {
    const std::string name = "atlas-" + std::to_string(page) + ".png";
    if (!writePng(directory / name, pages[page], result))
      return result;
    pageNames.push_back(name);
  }
  (void)writeMetadata(directory / "atlas.json",
                      {{"format_version", 1},
                       {"asset", asset.id},
                       {"pages", std::move(pageNames)},
                       {"sprites", std::move(metadataSprites)},
                       {"padding", padding},
                       {"bleed", bleed}},
                      result);
  return result;
}

std::optional<unsigned> parseHex(std::string_view text) {
  unsigned value = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value, 16);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
             ? std::optional<unsigned>{value}
             : std::nullopt;
}

std::vector<unsigned> glyphCodepoints(const nlohmann::json &settings,
                                      GeneratedAtlasCookResult &result,
                                      const std::filesystem::path &path) {
  const auto ranges =
      settings.value("glyph_ranges", std::vector<std::string>{"U+0020-U+007E"});
  std::set<unsigned> codepoints;
  for (const std::string &range : ranges) {
    const std::size_t separator = range.find("-U+");
    if (!range.starts_with("U+") || separator == std::string::npos) {
      error(result, "FONT_GLYPH_RANGE_INVALID",
            "Glyph ranges use U+XXXX-U+YYYY syntax.", path);
      continue;
    }
    const auto first =
        parseHex(std::string_view(range).substr(2, separator - 2));
    const auto last = parseHex(std::string_view(range).substr(separator + 3));
    if (!first || !last || *first > *last || *last > 0x10FFFFU ||
        *last - *first > 65535U) {
      error(result, "FONT_GLYPH_RANGE_INVALID",
            "Glyph range is invalid or exceeds 65536 codepoints.", path);
      continue;
    }
    for (unsigned codepoint = *first; codepoint <= *last; ++codepoint)
      codepoints.insert(codepoint);
  }
  return {codepoints.begin(), codepoints.end()};
}

GeneratedAtlasCookResult cookFontAtlas(const AssetManifest &asset,
                                       const std::filesystem::path &directory) {
  GeneratedAtlasCookResult result;
  std::ifstream input(asset.sourcePath, std::ios::binary | std::ios::ate);
  if (!input) {
    error(result, "FONT_SOURCE_NOT_FOUND", "Font source could not be read.",
          asset.sourcePath);
    return result;
  }
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  if (size <= 0 || !input.read(reinterpret_cast<char *>(bytes.data()), size)) {
    error(result, "FONT_SOURCE_INVALID", "Font source is empty or incomplete.",
          asset.sourcePath);
    return result;
  }
  stbtt_fontinfo font{};
  if (!stbtt_InitFont(&font, bytes.data(),
                      stbtt_GetFontOffsetForIndex(bytes.data(), 0))) {
    error(result, "FONT_SOURCE_INVALID", "Font source is not valid TTF/OTF.",
          asset.sourcePath);
    return result;
  }
  const nlohmann::json settings =
      nlohmann::json::parse(asset.settingsJson, nullptr, false);
  if (!settings.is_object()) {
    error(result, "FONT_ATLAS_SETTINGS_INVALID",
          "Font atlas settings must be a JSON object.", asset.manifestPath);
    return result;
  }
  const float pixelHeight = settings.value("pixel_height", 32.0F);
  const unsigned pageSize = settings.value("page_size", DefaultPageSize);
  const unsigned padding = settings.value("padding", 1U);
  if (!std::isfinite(pixelHeight) || pixelHeight <= 0.0F || pageSize == 0 ||
      pageSize > 8192) {
    error(result, "FONT_ATLAS_SETTINGS_INVALID",
          "Font atlas pixel height and page size must be positive.",
          asset.manifestPath);
    return result;
  }
  const auto codepoints = glyphCodepoints(settings, result, asset.manifestPath);
  if (hasErrors(result.diagnostics) || codepoints.empty())
    return result;
  const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
  std::vector<std::array<unsigned, 3>> cursors;
  std::vector<Image> pages;
  nlohmann::json glyphs = nlohmann::json::array();
  for (const unsigned codepoint : codepoints) {
    int width = 0;
    int height = 0;
    int xOffset = 0;
    int yOffset = 0;
    unsigned char *bitmap =
        stbtt_GetCodepointBitmap(&font, 0, scale, static_cast<int>(codepoint),
                                 &width, &height, &xOffset, &yOffset);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&font, static_cast<int>(codepoint), &advance,
                               &bearing);
    const unsigned glyphWidth = static_cast<unsigned>(std::max(width, 1));
    const unsigned glyphHeight = static_cast<unsigned>(std::max(height, 1));
    const Placement placement =
        place(glyphWidth, glyphHeight, pageSize, pageSize, padding, cursors,
              result, asset.manifestPath);
    if (hasErrors(result.diagnostics)) {
      stbtt_FreeBitmap(bitmap, nullptr);
      return result;
    }
    while (pages.size() <= placement.page)
      pages.push_back(
          {.width = pageSize,
           .height = pageSize,
           .rgba = std::vector<unsigned char>(
               static_cast<std::size_t>(pageSize) * pageSize * 4U)});
    if (bitmap != nullptr) {
      Image glyph{.width = static_cast<unsigned>(width),
                  .height = static_cast<unsigned>(height),
                  .rgba = std::vector<unsigned char>(
                      static_cast<std::size_t>(width) * height * 4U)};
      for (std::size_t index = 0;
           index < static_cast<std::size_t>(width) * height; ++index) {
        glyph.rgba[index * 4U] = 0xFFU;
        glyph.rgba[index * 4U + 1U] = 0xFFU;
        glyph.rgba[index * 4U + 2U] = 0xFFU;
        glyph.rgba[index * 4U + 3U] = bitmap[index];
      }
      blitWithBleed(pages[placement.page], glyph, placement, 0);
      stbtt_FreeBitmap(bitmap, nullptr);
    }
    glyphs.push_back(
        {{"codepoint", codepoint},
         {"page", placement.page},
         {"rect",
          {placement.x, placement.y, static_cast<unsigned>(std::max(width, 0)),
           static_cast<unsigned>(std::max(height, 0))}},
         {"offset", {xOffset, yOffset}},
         {"advance", advance * scale}});
  }
  nlohmann::json pageNames = nlohmann::json::array();
  for (std::size_t page = 0; page < pages.size(); ++page) {
    const std::string name = "font-" + std::to_string(page) + ".png";
    if (!writePng(directory / name, pages[page], result))
      return result;
    pageNames.push_back(name);
  }
  (void)writeMetadata(
      directory / "font-atlas.json",
      {{"format_version", 1},
       {"asset", asset.id},
       {"pixel_height", pixelHeight},
       {"pages", std::move(pageNames)},
       {"glyphs", std::move(glyphs)},
       {"fallbacks", settings.value("fallbacks", std::vector<std::string>{})}},
      result);
  return result;
}

} // namespace

GeneratedAtlasCookResult
cookGeneratedAtlas(const AssetManifest &asset, const AssetRegistry &registry,
                   const std::filesystem::path &outputDirectory) {
  const auto directory =
      outputDirectory / "generated/atlases" / idPath(asset.id);
  if (asset.type == "TextureAtlas2D")
    return cookTextureAtlas(asset, registry, directory);
  if (asset.type == "FontAtlas2D")
    return cookFontAtlas(asset, directory);
  return {};
}

Diagnostics validateGeneratedAtlasManifest(const AssetManifest &asset,
                                           const AssetRegistry &registry) {
  GeneratedAtlasCookResult result;
  if (asset.type == "TextureAtlas2D") {
    const auto descriptor = readJson(asset.sourcePath, result);
    if (!descriptor || !descriptor->is_object() ||
        descriptor->value("format_version", 0) != 1 ||
        !descriptor->contains("sprites") ||
        !(*descriptor)["sprites"].is_array() ||
        (*descriptor)["sprites"].empty()) {
      error(result, "ATLAS_DESCRIPTOR_INVALID",
            "Texture atlas descriptors require format_version 1 and sprites.",
            asset.sourcePath);
      return result.diagnostics;
    }
    try {
      const unsigned width = descriptor->value("page_width", DefaultPageSize);
      const unsigned height = descriptor->value("page_height", DefaultPageSize);
      const unsigned padding = descriptor->value("padding", 2U);
      const unsigned bleed = descriptor->value("bleed", 1U);
      if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
          bleed > padding)
        error(result, "ATLAS_SETTINGS_INVALID",
              "Atlas pages must be 1..8192 pixels and bleed cannot exceed "
              "padding.",
              asset.sourcePath);
      std::set<std::string> ids;
      for (const auto &entry : (*descriptor)["sprites"]) {
        if (!entry.is_object()) {
          error(result, "ATLAS_SPRITE_INVALID",
                "Atlas sprite entries must be objects.", asset.sourcePath);
          continue;
        }
        const std::string id = entry.value("id", "");
        const std::string sourceId = entry.value("source", "");
        if (!id.starts_with("asset://") || !sourceId.starts_with("asset://") ||
            !ids.insert(id).second) {
          error(result, "ATLAS_SPRITE_INVALID",
                "Atlas sprites require unique id and source asset:// "
                "references.",
                asset.sourcePath);
          continue;
        }
        if (findAsset(registry, sourceId) == nullptr)
          error(result, "ATLAS_SPRITE_SOURCE_NOT_FOUND",
                "Atlas sprite source was not found: " + sourceId,
                asset.sourcePath);
        if (std::ranges::find(asset.dependencies, sourceId) ==
            asset.dependencies.end())
          error(result, "ATLAS_DEPENDENCY_UNDECLARED",
                "Atlas sprite sources must be declared as dependencies: " +
                    sourceId,
                asset.manifestPath);
      }
    } catch (const nlohmann::json::exception &exception) {
      error(result, "ATLAS_DESCRIPTOR_INVALID", exception.what(),
            asset.sourcePath);
    }
  } else if (asset.type == "FontAtlas2D") {
    const nlohmann::json settings =
        nlohmann::json::parse(asset.settingsJson, nullptr, false);
    if (!settings.is_object()) {
      error(result, "FONT_ATLAS_SETTINGS_INVALID",
            "Font atlas settings must be a JSON object.", asset.manifestPath);
      return result.diagnostics;
    }
    try {
      const float pixelHeight = settings.value("pixel_height", 32.0F);
      const unsigned pageSize = settings.value("page_size", DefaultPageSize);
      if (!std::isfinite(pixelHeight) || pixelHeight <= 0.0F || pageSize == 0 ||
          pageSize > 8192)
        error(result, "FONT_ATLAS_SETTINGS_INVALID",
              "Font atlas pixel height and page size must be positive.",
              asset.manifestPath);
      (void)glyphCodepoints(settings, result, asset.manifestPath);
      for (const std::string &fallback :
           settings.value("fallbacks", std::vector<std::string>{})) {
        const AssetManifest *font = findAsset(registry, fallback);
        if (font == nullptr ||
            (font->type != "Font2D" && font->type != "FontAtlas2D"))
          error(result, "FONT_FALLBACK_NOT_FOUND",
                "Font fallback does not resolve to a font asset: " + fallback,
                asset.manifestPath);
        if (std::ranges::find(asset.dependencies, fallback) ==
            asset.dependencies.end())
          error(result, "FONT_FALLBACK_DEPENDENCY_UNDECLARED",
                "Font fallbacks must be declared as dependencies: " + fallback,
                asset.manifestPath);
      }
    } catch (const nlohmann::json::exception &exception) {
      error(result, "FONT_ATLAS_SETTINGS_INVALID", exception.what(),
            asset.manifestPath);
    }
  }
  return result.diagnostics;
}

} // namespace demi::assets
