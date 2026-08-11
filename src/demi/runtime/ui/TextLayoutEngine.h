#pragma once

#include "demi/runtime/ui/UiModel.h"
#include "demi/runtime/ui/TextShaper.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <unordered_map>

namespace demi::runtime::ui {

enum class TextWrap { None, Word, Grapheme };
enum class TextHorizontalAlignment { Start, Center, End };
enum class TextVerticalAlignment { Start, Center, End };
enum class TextOverflow { Visible, Clip, Ellipsis };

struct TextLayoutRequest {
  std::string text;
  float width = 0.0F;
  float height = 0.0F;
  float fontSize = 20.0F;
  float lineSpacing = 0.0F;
  TextWrap wrap = TextWrap::Word;
  TextHorizontalAlignment horizontal = TextHorizontalAlignment::Start;
  TextVerticalAlignment vertical = TextVerticalAlignment::Start;
  TextOverflow overflow = TextOverflow::Clip;
  std::size_t maxLines = 0;
  TextDirection direction = TextDirection::Auto;
  std::string locale;
  std::string font;
  std::uint64_t fontRevision = 0;
};

struct TextLineLayout {
  std::string text;
  std::size_t graphemeStart = 0;
  std::size_t graphemeCount = 0;
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  TextShapeResult shaped;
};

struct TextLayoutResult {
  std::vector<TextLineLayout> lines;
  float width = 0.0F;
  float height = 0.0F;
  std::size_t graphemeCount = 0;
  bool truncated = false;
  bool validUtf8 = true;
  bool shapingComplete = true;
  struct Caret {
    std::size_t grapheme = 0;
    std::size_t line = 0;
    float x = 0.0F;
    float y = 0.0F;
    float height = 0.0F;
  };
  std::vector<Caret> carets;
  struct VisualSegment {
    std::size_t graphemeStart = 0;
    std::size_t graphemeCount = 0;
    std::size_t line = 0;
    float x = 0.0F;
    float width = 0.0F;
  };
  std::vector<VisualSegment> visualSegments;
};

using TextMeasure = std::function<float(std::string_view)>;
using TextShape = std::function<TextShapeResult(std::string_view)>;

class TextLayoutEngine {
public:
  [[nodiscard]] TextLayoutResult layout(const TextLayoutRequest &request,
                                        const TextMeasure &measure = {},
                                        const TextShape &shape = {}) const;
  [[nodiscard]] static std::size_t graphemeCount(std::string_view text);
  [[nodiscard]] static std::optional<std::string>
  graphemeSlice(std::string_view text, std::size_t first,
                std::size_t count);
  [[nodiscard]] static std::size_t hitTest(const TextLayoutResult &layout,
                                           Vec2 point);
  [[nodiscard]] static std::vector<Rect>
  selectionRects(const TextLayoutResult &layout, std::size_t first,
                 std::size_t count);
};

struct TextLayoutCacheStats {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t entries = 0;
};

class TextLayoutCache {
public:
  explicit TextLayoutCache(std::size_t capacity = 256)
      : capacity_(capacity == 0 ? 1 : capacity) {}
  [[nodiscard]] const TextLayoutResult &layout(const TextLayoutRequest &request);
  void clear();
  [[nodiscard]] TextLayoutCacheStats stats() const;

private:
  struct Entry { std::string key; TextLayoutResult value; };
  std::size_t capacity_;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
  std::list<Entry> entries_;
  std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

} // namespace demi::runtime::ui
