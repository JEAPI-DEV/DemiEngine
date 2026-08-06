#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::ui {

struct RichTextSpan {
  std::size_t begin = 0;
  std::size_t length = 0;
  std::string style;
  std::string value;
};

struct RichTextDocument {
  std::string text;
  std::vector<RichTextSpan> spans;
  std::vector<std::string> diagnostics;
};

class RichTextParser {
public:
  [[nodiscard]] RichTextDocument parse(std::string_view markup,
                                       bool strict = true) const;
};

} // namespace demi::runtime::ui
