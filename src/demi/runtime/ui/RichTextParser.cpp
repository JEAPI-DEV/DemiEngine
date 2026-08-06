#include "demi/runtime/ui/RichTextParser.h"

#include <array>
#include <optional>

namespace demi::runtime::ui {
namespace {
struct OpenSpan { std::string name; std::string value; std::size_t begin; };
bool allowed(const std::string_view name) {
  constexpr std::array names{"color", "em", "strong", "link", "icon"};
  for (const auto candidate : names)
    if (name == candidate)
      return true;
  return false;
}
} // namespace

RichTextDocument RichTextParser::parse(const std::string_view markup,
                                       const bool strict) const {
  RichTextDocument out;
  std::vector<OpenSpan> stack;
  for (std::size_t cursor = 0; cursor < markup.size();) {
    if (markup[cursor] != '[') {
      out.text.push_back(markup[cursor++]);
      continue;
    }
    const std::size_t close = markup.find(']', cursor + 1);
    if (close == std::string_view::npos) {
      out.diagnostics.emplace_back("Unclosed rich-text tag.");
      if (!strict)
        out.text.append(markup.substr(cursor));
      break;
    }
    std::string token(markup.substr(cursor + 1, close - cursor - 1));
    const bool closing = !token.empty() && token.front() == '/';
    if (closing)
      token.erase(token.begin());
    const std::size_t equals = token.find('=');
    const std::string name = token.substr(0, equals);
    const std::string value = equals == std::string::npos
                                  ? std::string{}
                                  : token.substr(equals + 1);
    if (!allowed(name)) {
      out.diagnostics.push_back("Unknown rich-text tag: " + name);
      if (!strict)
        out.text.append(markup.substr(cursor, close - cursor + 1));
    } else if (name == "icon" && !closing) {
      out.spans.push_back({.begin = out.text.size(),
                           .length = 0,
                           .style = name,
                           .value = value});
    } else if (!closing) {
      stack.push_back({name, value, out.text.size()});
    } else if (stack.empty() || stack.back().name != name) {
      out.diagnostics.push_back("Mismatched rich-text closing tag: " + name);
    } else {
      const OpenSpan open = std::move(stack.back());
      stack.pop_back();
      out.spans.push_back({.begin = open.begin,
                           .length = out.text.size() - open.begin,
                           .style = open.name,
                           .value = open.value});
    }
    cursor = close + 1;
  }
  for (const auto &open : stack)
    out.diagnostics.push_back("Unclosed rich-text tag: " + open.name);
  return out;
}

} // namespace demi::runtime::ui
