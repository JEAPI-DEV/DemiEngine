#include "demi/runtime/ui/TextShaper.h"

#include <SheenBidi/SBAlgorithm.h>
#include <SheenBidi/SBLine.h>
#include <SheenBidi/SBParagraph.h>
#include <hb-ot.h>
#include <utf8proc.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {

struct HbBlobDeleter {
  void operator()(hb_blob_t *value) const { hb_blob_destroy(value); }
};
struct HbFaceDeleter {
  void operator()(hb_face_t *value) const { hb_face_destroy(value); }
};
struct HbFontDeleter {
  void operator()(hb_font_t *value) const { hb_font_destroy(value); }
};
struct HbBufferDeleter {
  void operator()(hb_buffer_t *value) const { hb_buffer_destroy(value); }
};

using HbBlob = std::unique_ptr<hb_blob_t, HbBlobDeleter>;
using HbFace = std::unique_ptr<hb_face_t, HbFaceDeleter>;
using HbFont = std::unique_ptr<hb_font_t, HbFontDeleter>;
using HbBuffer = std::unique_ptr<hb_buffer_t, HbBufferDeleter>;

HbFont makeFont(const TextFontFace &source, const float fontSize) {
  if (!source.data || source.data->empty())
    return {};
  HbBlob blob(hb_blob_create(
      reinterpret_cast<const char *>(source.data->data()),
      static_cast<unsigned int>(source.data->size()), HB_MEMORY_MODE_READONLY,
      nullptr, nullptr));
  if (!blob)
    return {};
  HbFace face(hb_face_create(blob.get(), 0));
  if (!face || hb_face_get_glyph_count(face.get()) == 0)
    return {};
  HbFont font(hb_font_create(face.get()));
  hb_ot_font_set_funcs(font.get());
  const int scale = std::max(1, static_cast<int>(std::lround(fontSize * 64.0F)));
  hb_font_set_scale(font.get(), scale, scale);
  return font;
}

bool decode(std::string_view text,
            std::vector<std::pair<std::uint32_t, std::size_t>> &result,
            std::vector<std::size_t> &clusterStarts) {
  utf8proc_int32_t previous = -1;
  utf8proc_int32_t state = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    utf8proc_int32_t codepoint = 0;
    const auto length = utf8proc_iterate(
        reinterpret_cast<const utf8proc_uint8_t *>(text.data() + offset),
        static_cast<utf8proc_ssize_t>(text.size() - offset), &codepoint);
    if (length <= 0)
      return false;
    if (previous < 0 ||
        utf8proc_grapheme_break_stateful(previous, codepoint, &state) != 0)
      clusterStarts.push_back(offset);
    result.emplace_back(static_cast<std::uint32_t>(codepoint), offset);
    previous = codepoint;
    offset += static_cast<std::size_t>(length);
  }
  return true;
}

std::size_t fontIndexFor(const FontResolver &fonts,
                         const std::uint32_t codepoint) {
  const TextFontFace *resolved = fonts.resolve(codepoint);
  for (std::size_t index = 0; index < fonts.size(); ++index)
    if (fonts.font(index) == resolved)
      return index;
  return 0;
}

} // namespace

bool FontResolver::add(std::string id, const std::span<const std::byte> data,
                       const std::uint64_t revision, std::string &error) {
  if (id.empty() || data.empty()) {
    error = "A fallback font requires a non-empty ID and font data.";
    return false;
  }
  if (std::ranges::any_of(fonts_, [&](const TextFontFace &font) {
        return font.id == id;
      })) {
    error = "Duplicate fallback font ID: " + id;
    return false;
  }
  auto bytes = std::make_shared<std::vector<std::byte>>(data.begin(), data.end());
  TextFontFace probe{.id = id, .data = bytes, .revision = revision};
  if (!makeFont(probe, 16.0F)) {
    error = "Invalid OpenType font data for: " + id;
    return false;
  }
  fonts_.push_back(std::move(probe));
  coverageCache_.clear();
  error.clear();
  return true;
}

void FontResolver::clear() { fonts_.clear(); coverageCache_.clear(); }

const TextFontFace *FontResolver::font(const std::size_t index) const {
  return index < fonts_.size() ? &fonts_[index] : nullptr;
}

const TextFontFace *FontResolver::resolve(const std::uint32_t codepoint) const {
  constexpr std::size_t Missing = std::numeric_limits<std::size_t>::max();
  if (const auto cached = coverageCache_.find(codepoint);
      cached != coverageCache_.end())
    return cached->second == Missing ? nullptr : &fonts_[cached->second];
  if (coverageCache_.size() >= coverageCacheLimit())
    coverageCache_.clear();
  for (std::size_t index = 0; index < fonts_.size(); ++index) {
    const auto &candidate = fonts_[index];
    HbFont font = makeFont(candidate, 16.0F);
    hb_codepoint_t glyph = 0;
    if (font && hb_font_get_nominal_glyph(font.get(), codepoint, &glyph) &&
        glyph != 0)
      return coverageCache_[codepoint] = index, &candidate;
  }
  coverageCache_[codepoint] = Missing;
  return nullptr;
}

const TextFontFace *
FontResolver::resolve(const std::span<const std::uint32_t> codepoints) const {
  if (codepoints.empty())
    return fonts_.empty() ? nullptr : &fonts_.front();
  for (const auto &candidate : fonts_) {
    HbFont font = makeFont(candidate, 16.0F);
    if (!font)
      continue;
    bool complete = true;
    for (const std::uint32_t codepoint : codepoints) {
      hb_codepoint_t glyph = 0;
      if (!hb_font_get_nominal_glyph(font.get(), codepoint, &glyph) ||
          glyph == 0) {
        complete = false;
        break;
      }
    }
    if (complete)
      return &candidate;
  }
  return nullptr;
}

std::uint64_t FontResolver::revision() const {
  std::uint64_t value = 1469598103934665603ULL;
  for (const auto &font : fonts_) {
    value ^= font.revision;
    value *= 1099511628211ULL;
    for (const unsigned char byte : font.id) {
      value ^= byte;
      value *= 1099511628211ULL;
    }
  }
  return value;
}

TextShapeResult TextShaper::shape(const TextShapeRequest &request,
                                  const FontResolver &fonts) const {
  TextShapeResult result;
  result.fontSize = request.fontSize;
  if (!std::isfinite(request.fontSize) || request.fontSize <= 0.0F ||
      fonts.size() == 0) {
    result.complete = false;
    return result;
  }
  std::vector<std::pair<std::uint32_t, std::size_t>> codepoints;
  std::vector<std::size_t> clusterStarts;
  result.validUtf8 = decode(request.text, codepoints, clusterStarts);
  if (!result.validUtf8) {
    result.complete = false;
    return result;
  }
  for (const auto &[codepoint, byteOffset] : codepoints) {
    if (codepoint != '\n' && codepoint != '\r' && codepoint != '\t' &&
        fonts.resolve(codepoint) == nullptr)
      result.missingGlyphs.push_back({codepoint, byteOffset});
  }
  result.complete = result.missingGlyphs.empty();
  if (request.text.empty())
    return result;

  const SBCodepointSequence sequence{
      .stringEncoding = SBStringEncodingUTF8,
      .stringBuffer = request.text.data(),
      .stringLength = static_cast<SBUInteger>(request.text.size())};
  const SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
  if (algorithm == nullptr) {
    result.complete = false;
    return result;
  }

  std::size_t paragraphOffset = 0;
  while (paragraphOffset < request.text.size()) {
    SBUInteger paragraphLength = 0;
    SBUInteger separatorLength = 0;
    SBAlgorithmGetParagraphBoundary(
        algorithm, static_cast<SBUInteger>(paragraphOffset),
        static_cast<SBUInteger>(request.text.size() - paragraphOffset),
        &paragraphLength, &separatorLength);
    if (paragraphLength == 0)
      break;
    const SBLevel level = request.direction == TextDirection::LeftToRight
                              ? 0
                          : request.direction == TextDirection::RightToLeft
                              ? 1
                              : SBLevelDefaultLTR;
    const SBParagraphRef paragraph = SBAlgorithmCreateParagraph(
        algorithm, static_cast<SBUInteger>(paragraphOffset), paragraphLength,
        level);
    const SBUInteger contentLength = paragraphLength - separatorLength;
    const SBLineRef line = paragraph == nullptr
                               ? nullptr
                               : SBParagraphCreateLine(
                                     paragraph,
                                     static_cast<SBUInteger>(paragraphOffset),
                                     contentLength);
    if (line == nullptr) {
      result.complete = false;
    } else {
      const SBRun *bidiRuns = SBLineGetRunsPtr(line);
      const SBUInteger bidiRunCount = SBLineGetRunCount(line);
      for (SBUInteger bidiIndex = 0; bidiIndex < bidiRunCount; ++bidiIndex) {
        const SBRun &bidi = bidiRuns[bidiIndex];
        std::size_t segment = bidi.offset;
        const std::size_t runEnd = bidi.offset + bidi.length;
        while (segment < runEnd) {
          const auto clusterAt = std::ranges::lower_bound(clusterStarts, segment);
          const std::size_t firstByte = clusterAt == clusterStarts.end()
                                            ? segment
                                            : *clusterAt;
          const auto nextCluster = std::ranges::upper_bound(clusterStarts, firstByte);
          std::size_t clusterEnd = nextCluster == clusterStarts.end()
                                       ? runEnd
                                       : std::min(*nextCluster, runEnd);
          const auto fontForCluster = [&](const std::size_t begin,
                                          const std::size_t end) {
            std::vector<std::uint32_t> values;
            for (const auto &[codepoint, byteOffset] : codepoints)
              if (byteOffset >= begin && byteOffset < end)
                values.push_back(codepoint);
            const TextFontFace *resolved = fonts.resolve(values);
            return resolved == nullptr
                       ? fontIndexFor(fonts, values.empty() ? 0 : values.front())
                       : static_cast<std::size_t>(resolved - fonts.font(0));
          };
          const std::size_t selectedFont = fontForCluster(firstByte, clusterEnd);
          std::size_t segmentEnd = clusterEnd;
          while (segmentEnd < runEnd) {
            const auto following = std::ranges::upper_bound(clusterStarts,
                                                             segmentEnd);
            const std::size_t followingEnd =
                following == clusterStarts.end() ? runEnd
                                                 : std::min(*following, runEnd);
            if (fontForCluster(segmentEnd, followingEnd) != selectedFont)
              break;
            segmentEnd = followingEnd;
          }
          const TextFontFace *face = fonts.font(selectedFont);
          HbFont font = face == nullptr ? HbFont{} : makeFont(*face, request.fontSize);
          HbBuffer buffer(hb_buffer_create());
          if (!font || !buffer) {
            result.complete = false;
            break;
          }
          hb_buffer_add_utf8(buffer.get(), request.text.data(),
                             static_cast<int>(request.text.size()),
                             static_cast<unsigned int>(segment),
                             static_cast<int>(segmentEnd - segment));
          hb_buffer_set_direction(buffer.get(),
                                  (bidi.level & 1U) != 0U ? HB_DIRECTION_RTL
                                                         : HB_DIRECTION_LTR);
          hb_buffer_guess_segment_properties(buffer.get());
          if (!request.locale.empty())
            hb_buffer_set_language(
                buffer.get(), hb_language_from_string(request.locale.data(),
                                                       request.locale.size()));
          hb_shape(font.get(), buffer.get(), nullptr, 0);
          unsigned int glyphCount = 0;
          const hb_glyph_info_t *info =
              hb_buffer_get_glyph_infos(buffer.get(), &glyphCount);
          const hb_glyph_position_t *positions =
              hb_buffer_get_glyph_positions(buffer.get(), &glyphCount);
          ShapedRun shaped{.byteOffset = segment,
                           .byteLength = segmentEnd - segment,
                           .direction = (bidi.level & 1U) != 0U
                                            ? TextDirection::RightToLeft
                                            : TextDirection::LeftToRight,
                           .glyphs = {},
                           .advance = 0.0F};
          shaped.glyphs.reserve(glyphCount);
          for (unsigned int index = 0; index < glyphCount; ++index) {
            shaped.glyphs.push_back(
                {.fontIndex = selectedFont,
                 .glyphId = info[index].codepoint,
                 .byteOffset = info[index].cluster,
                 .xAdvance = positions[index].x_advance / 64.0F,
                 .yAdvance = positions[index].y_advance / 64.0F,
                 .xOffset = positions[index].x_offset / 64.0F,
                 .yOffset = -positions[index].y_offset / 64.0F});
            shaped.advance += positions[index].x_advance / 64.0F;
          }
          result.advance += shaped.advance;
          result.runs.push_back(std::move(shaped));
          segment = segmentEnd;
        }
      }
      SBLineRelease(line);
    }
    if (paragraph != nullptr)
      SBParagraphRelease(paragraph);
    paragraphOffset += paragraphLength;
  }
  SBAlgorithmRelease(algorithm);
  return result;
}

} // namespace demi::runtime::ui
