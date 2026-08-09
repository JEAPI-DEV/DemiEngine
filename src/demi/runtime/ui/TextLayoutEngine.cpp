#include "demi/runtime/ui/TextLayoutEngine.h"

#include <utf8proc.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime::ui {
namespace {

struct Cluster {
  std::size_t begin = 0;
  std::size_t end = 0;
  bool whitespace = false;
  bool newline = false;
};

std::vector<Cluster> clusters(const std::string_view text, bool &valid,
                              bool &needsShaping) {
  std::vector<Cluster> result;
  valid = true;
  needsShaping = false;
  std::size_t offset = 0;
  utf8proc_int32_t previous = -1;
  utf8proc_int32_t state = 0;
  while (offset < text.size()) {
    utf8proc_int32_t codepoint = 0;
    const auto bytes = utf8proc_iterate(
        reinterpret_cast<const utf8proc_uint8_t *>(text.data() + offset),
        static_cast<utf8proc_ssize_t>(text.size() - offset), &codepoint);
    if (bytes <= 0) {
      valid = false;
      return {};
    }
    const std::size_t next = offset + static_cast<std::size_t>(bytes);
    const bool boundary = previous < 0 ||
                          utf8proc_grapheme_break_stateful(previous, codepoint,
                                                          &state) != 0;
    if (boundary)
      result.push_back({.begin = offset, .end = next});
    else
      result.back().end = next;
    Cluster &cluster = result.back();
    cluster.newline = cluster.newline || codepoint == '\n';
    cluster.whitespace = cluster.whitespace ||
                         codepoint == ' ' || codepoint == '\t';
    const auto category = utf8proc_category(codepoint);
    needsShaping = needsShaping || codepoint > 0x7f ||
                   category == UTF8PROC_CATEGORY_MN ||
                   category == UTF8PROC_CATEGORY_MC;
    previous = codepoint;
    offset = next;
  }
  return result;
}

float defaultMeasure(const std::string_view value, const float fontSize) {
  bool valid = true;
  bool shaping = false;
  return static_cast<float>(clusters(value, valid, shaping).size()) *
         fontSize * 0.6F;
}

} // namespace

TextLayoutResult TextLayoutEngine::layout(const TextLayoutRequest &request,
                                          const TextMeasure &measure,
                                          const TextShape &shape) const {
  TextLayoutResult result;
  bool needsShaping = false;
  const auto graphemes = clusters(request.text, result.validUtf8, needsShaping);
  result.graphemeCount = graphemes.size();
  result.shapingComplete = !needsShaping || static_cast<bool>(shape);
  if (!result.validUtf8 || request.fontSize <= 0.0F)
    return result;

  if (graphemes.empty()) {
    const float verticalSpace =
        std::max(request.height - request.fontSize, 0.0F);
    const float y = request.vertical == TextVerticalAlignment::Center
                        ? verticalSpace * 0.5F
                    : request.vertical == TextVerticalAlignment::End
                        ? verticalSpace
                        : 0.0F;
    const float x = request.horizontal == TextHorizontalAlignment::Center
                        ? std::max(request.width, 0.0F) * 0.5F
                    : request.horizontal == TextHorizontalAlignment::End
                        ? std::max(request.width, 0.0F)
                        : 0.0F;
    result.lines.push_back({.text = {},
                            .graphemeStart = 0,
                            .graphemeCount = 0,
                            .x = x,
                            .y = y,
                            .width = 0.0F});
    result.height = request.fontSize;
    result.carets.push_back(
        {.grapheme = 0, .line = 0, .x = x, .y = y,
         .height = request.fontSize});
    return result;
  }

  const auto widthOf = [&](const std::string_view value) {
    return measure ? measure(value) : defaultMeasure(value, request.fontSize);
  };
  const float available = std::max(request.width, 0.0F);
  const bool constrained = request.wrap != TextWrap::None && available > 0.0F;
  std::size_t lineStart = 0;
  std::size_t index = 0;
  auto append = [&](std::size_t begin, std::size_t end) {
    const std::size_t byteBegin = graphemes[begin].begin;
    const std::size_t byteEnd = end > begin ? graphemes[end - 1].end : byteBegin;
    std::string value(request.text.substr(byteBegin, byteEnd - byteBegin));
    result.lines.push_back({.text = std::move(value),
                            .graphemeStart = begin,
                            .graphemeCount = end - begin});
    result.lines.back().width = widthOf(result.lines.back().text);
  };

  while (index < graphemes.size()) {
    if (graphemes[index].newline) {
      append(lineStart, index);
      lineStart = ++index;
      continue;
    }
    const auto byteBegin = graphemes[lineStart].begin;
    const auto byteEnd = graphemes[index].end;
    if (constrained && index > lineStart &&
        widthOf(request.text.substr(byteBegin, byteEnd - byteBegin)) >
            available) {
      std::size_t breakAt = index;
      if (request.wrap == TextWrap::Word) {
        for (std::size_t candidate = index; candidate > lineStart; --candidate)
          if (graphemes[candidate - 1].whitespace) {
            breakAt = candidate - 1;
            break;
          }
      }
      if (breakAt == lineStart)
        breakAt = index;
      append(lineStart, breakAt);
      lineStart = breakAt;
      while (lineStart < graphemes.size() && graphemes[lineStart].whitespace)
        ++lineStart;
      index = lineStart;
      continue;
    }
    ++index;
  }
  if (lineStart < graphemes.size())
    append(lineStart, graphemes.size());
  else if (!request.text.empty() && request.text.back() == '\n')
    result.lines.push_back({.text = {},
                            .graphemeStart = graphemes.size(),
                            .graphemeCount = 0,
                            .x = 0.0F,
                            .y = 0.0F,
                            .width = 0.0F});

  const float lineHeight = request.fontSize + request.lineSpacing;
  std::size_t limit = result.lines.size();
  if (request.maxLines > 0)
    limit = std::min(limit, request.maxLines);
  if (request.height > 0.0F)
    limit = std::min(limit, static_cast<std::size_t>(
                                std::max(std::floor((request.height +
                                                    request.lineSpacing) /
                                                   std::max(lineHeight, 0.001F)),
                                         0.0F)));
  if (limit < result.lines.size()) {
    result.lines.resize(limit);
    result.truncated = true;
  }
  if (result.truncated && request.overflow == TextOverflow::Ellipsis &&
      !result.lines.empty()) {
    auto &last = result.lines.back();
    constexpr std::string_view ellipsis = "...";
    while (!last.text.empty() && available > 0.0F &&
           widthOf(last.text + std::string(ellipsis)) > available) {
      const auto sliced = graphemeSlice(last.text, 0, last.graphemeCount - 1);
      last.text = sliced.value_or(std::string{});
      --last.graphemeCount;
    }
    last.text += ellipsis;
    last.width = widthOf(last.text);
  }

  result.height = result.lines.empty()
                      ? 0.0F
                      : request.fontSize +
                            (result.lines.size() - 1) * lineHeight;
  const float verticalSpace = std::max(request.height - result.height, 0.0F);
  const float yOffset = request.vertical == TextVerticalAlignment::Center
                            ? verticalSpace * 0.5F
                        : request.vertical == TextVerticalAlignment::End
                            ? verticalSpace
                            : 0.0F;
  for (std::size_t line = 0; line < result.lines.size(); ++line) {
    auto &item = result.lines[line];
    if (shape) {
      item.shaped = shape(item.text);
      item.width = item.shaped.advance;
      result.validUtf8 = result.validUtf8 && item.shaped.validUtf8;
      result.shapingComplete = result.shapingComplete && item.shaped.complete;
    }
    const float horizontalSpace = std::max(request.width - item.width, 0.0F);
    item.x = request.horizontal == TextHorizontalAlignment::Center
                 ? horizontalSpace * 0.5F
             : request.horizontal == TextHorizontalAlignment::End
                 ? horizontalSpace
                 : 0.0F;
    item.y = yOffset + line * lineHeight;
    result.width = std::max(result.width, item.width);
    if (shape && !item.shaped.runs.empty()) {
      bool localValid = true;
      bool localShaping = false;
      const auto localClusters = clusters(item.text, localValid, localShaping);
      std::vector<std::optional<float>> caretX(item.graphemeCount + 1);
      float visualX = item.x;
      for (const auto &run : item.shaped.runs) {
        std::vector<std::size_t> clusterBytes;
        for (const auto &glyph : run.glyphs)
          clusterBytes.push_back(glyph.byteOffset);
        std::ranges::sort(clusterBytes);
        clusterBytes.erase(std::unique(clusterBytes.begin(), clusterBytes.end()),
                           clusterBytes.end());
        std::size_t glyph = 0;
        while (glyph < run.glyphs.size()) {
          const std::size_t clusterByte = run.glyphs[glyph].byteOffset;
          const float segmentX = visualX;
          while (glyph < run.glyphs.size() &&
                 run.glyphs[glyph].byteOffset == clusterByte) {
            visualX += run.glyphs[glyph].xAdvance;
            ++glyph;
          }
          const auto nextByte = std::ranges::upper_bound(clusterBytes, clusterByte);
          const std::size_t clusterEnd =
              nextByte == clusterBytes.end() ? run.byteOffset + run.byteLength
                                             : *nextByte;
          const auto firstCluster = std::ranges::lower_bound(
              localClusters, clusterByte, {}, &Cluster::begin);
          const auto endCluster = std::ranges::lower_bound(
              localClusters, clusterEnd, {}, &Cluster::begin);
          const std::size_t first = static_cast<std::size_t>(
              std::distance(localClusters.begin(), firstCluster));
          const std::size_t end = std::max(
              first + 1,
              static_cast<std::size_t>(std::distance(localClusters.begin(),
                                                      endCluster)));
          if (first < caretX.size()) {
            const float left = std::min(segmentX, visualX);
            const float right = std::max(segmentX, visualX);
            const bool rtl = run.direction == TextDirection::RightToLeft;
            caretX[first] = rtl ? right : left;
            if (std::min(end, caretX.size() - 1) < caretX.size())
              caretX[std::min(end, caretX.size() - 1)] = rtl ? left : right;
            result.visualSegments.push_back(
                {.graphemeStart = item.graphemeStart + first,
                 .graphemeCount = std::min(end, item.graphemeCount) - first,
                 .line = line,
                 .x = left,
                 .width = right - left});
          }
        }
      }
      for (std::size_t local = 0; local < caretX.size(); ++local) {
        if (!caretX[local]) {
          const auto prefix = graphemeSlice(item.text, 0, local);
          caretX[local] = item.x + widthOf(prefix.value_or(std::string{}));
        }
        result.carets.push_back({.grapheme = item.graphemeStart + local,
                                 .line = line,
                                 .x = *caretX[local],
                                 .y = item.y,
                                 .height = request.fontSize});
      }
    } else {
      for (std::size_t local = 0; local <= item.graphemeCount; ++local) {
        const auto prefix = graphemeSlice(item.text, 0, local);
        const float x = item.x + widthOf(prefix.value_or(std::string{}));
        result.carets.push_back({.grapheme = item.graphemeStart + local,
                                 .line = line,
                                 .x = x,
                                 .y = item.y,
                                 .height = request.fontSize});
        if (local < item.graphemeCount) {
          const auto next = graphemeSlice(item.text, 0, local + 1);
          result.visualSegments.push_back(
              {.graphemeStart = item.graphemeStart + local,
               .graphemeCount = 1,
               .line = line,
               .x = x,
               .width = widthOf(next.value_or(std::string{})) - (x - item.x)});
        }
      }
    }
  }
  return result;
}

std::size_t TextLayoutEngine::hitTest(const TextLayoutResult &layout,
                                      const Vec2 point) {
  if (layout.carets.empty()) return 0;
  const auto closest = std::ranges::min_element(
      layout.carets, {}, [&](const TextLayoutResult::Caret &caret) {
        const float dx = caret.x - point.x;
        const float dy = (caret.y + caret.height * 0.5F) - point.y;
        return dx * dx + dy * dy;
      });
  return closest->grapheme;
}

std::vector<Rect> TextLayoutEngine::selectionRects(
    const TextLayoutResult &layout, const std::size_t first,
    const std::size_t count) {
  std::vector<Rect> result;
  const std::size_t last = first + count;
  for (const auto &segment : layout.visualSegments) {
    const std::size_t begin = std::max(first, segment.graphemeStart);
    const std::size_t end = std::min(last, segment.graphemeStart +
                                              segment.graphemeCount);
    if (end <= begin)
      continue;
    const auto caret = std::ranges::find_if(layout.carets, [&](const auto &value) {
      return value.line == segment.line && value.grapheme == begin;
    });
    result.push_back({segment.x, caret == layout.carets.end() ? 0.0F : caret->y,
                      segment.width,
                      caret == layout.carets.end() ? 0.0F : caret->height});
  }
  return result;
}

std::size_t TextLayoutEngine::graphemeCount(const std::string_view text) {
  bool valid = true;
  bool shaping = false;
  const auto value = clusters(text, valid, shaping);
  return valid ? value.size() : 0;
}

std::optional<std::string>
TextLayoutEngine::graphemeSlice(const std::string_view text,
                                const std::size_t first,
                                const std::size_t count) {
  bool valid = true;
  bool shaping = false;
  const auto value = clusters(text, valid, shaping);
  if (!valid || first > value.size())
    return std::nullopt;
  if (first == value.size() || count == 0)
    return std::string{};
  const std::size_t end = std::min(first + count, value.size());
  return std::string(text.substr(value[first].begin,
                                 value[end - 1].end - value[first].begin));
}

namespace {
std::string cacheKey(const TextLayoutRequest &request) {
  return request.text + '\x1f' + std::to_string(request.width) + ':' +
         std::to_string(request.height) + ':' + std::to_string(request.fontSize) +
         ':' + std::to_string(request.lineSpacing) + ':' +
         std::to_string(static_cast<int>(request.wrap)) + ':' +
         std::to_string(static_cast<int>(request.horizontal)) + ':' +
         std::to_string(static_cast<int>(request.vertical)) + ':' +
         std::to_string(static_cast<int>(request.overflow)) + ':' +
         std::to_string(request.maxLines) + ':' +
         std::to_string(static_cast<int>(request.direction)) + ':' +
         request.locale + ':' + std::to_string(request.fontRevision);
}
} // namespace

const TextLayoutResult &TextLayoutCache::layout(const TextLayoutRequest &request) {
  const std::string key = cacheKey(request);
  if (const auto found = index_.find(key); found != index_.end()) {
    ++hits_;
    entries_.splice(entries_.begin(), entries_, found->second);
    return entries_.front().value;
  }
  ++misses_;
  entries_.push_front({key, TextLayoutEngine{}.layout(request)});
  index_[key] = entries_.begin();
  while (entries_.size() > capacity_) {
    index_.erase(entries_.back().key);
    entries_.pop_back();
  }
  return entries_.front().value;
}

void TextLayoutCache::clear() { entries_.clear(); index_.clear(); }
TextLayoutCacheStats TextLayoutCache::stats() const {
  return {hits_, misses_, entries_.size()};
}

} // namespace demi::runtime::ui
