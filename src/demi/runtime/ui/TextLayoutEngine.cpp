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
                                          const TextMeasure &measure) const {
  TextLayoutResult result;
  bool needsShaping = false;
  const auto graphemes = clusters(request.text, result.validUtf8, needsShaping);
  result.graphemeCount = graphemes.size();
  result.shapingComplete = !needsShaping;
  if (!result.validUtf8 || graphemes.empty() || request.fontSize <= 0.0F)
    return result;

  const auto widthOf = [&](const std::string_view value) {
    return measure ? measure(value) : defaultMeasure(value, request.fontSize);
  };
  const float available = std::max(request.width, 0.0F);
  const bool constrained = request.wrap != TextWrap::None && available > 0.0F;
  std::size_t lineStart = 0;
  std::size_t index = 0;
  auto append = [&](std::size_t begin, std::size_t end) {
    while (end > begin && graphemes[end - 1].whitespace)
      --end;
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
    const float horizontalSpace = std::max(request.width - item.width, 0.0F);
    item.x = request.horizontal == TextHorizontalAlignment::Center
                 ? horizontalSpace * 0.5F
             : request.horizontal == TextHorizontalAlignment::End
                 ? horizontalSpace
                 : 0.0F;
    item.y = yOffset + line * lineHeight;
    result.width = std::max(result.width, item.width);
    for (std::size_t local = 0; local <= item.graphemeCount; ++local) {
      const auto prefix = graphemeSlice(item.text, 0, local);
      result.carets.push_back({.grapheme = item.graphemeStart + local,
                               .line = line,
                               .x = item.x + widthOf(prefix.value_or(std::string{})),
                               .y = item.y,
                               .height = request.fontSize});
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
  for (std::size_t line = 0; line < layout.lines.size(); ++line) {
    const auto &item = layout.lines[line];
    const std::size_t begin = std::max(first, item.graphemeStart);
    const std::size_t end = std::min(last, item.graphemeStart + item.graphemeCount);
    if (end <= begin) continue;
    const auto left = std::ranges::find_if(layout.carets, [&](const auto &caret) {
      return caret.line == line && caret.grapheme == begin;
    });
    const auto right = std::ranges::find_if(layout.carets, [&](const auto &caret) {
      return caret.line == line && caret.grapheme == end;
    });
    if (left != layout.carets.end() && right != layout.carets.end())
      result.push_back({left->x, left->y, std::max(right->x - left->x, 0.0F),
                        left->height});
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
         std::to_string(request.maxLines);
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
