#pragma once

#include <string_view>

namespace demi::runtime::scripting {

class RegexMatcher {
public:
  [[nodiscard]] bool isValid(std::string_view pattern) const;
  [[nodiscard]] bool matches(std::string_view value, std::string_view pattern,
                             bool caseSensitive = false) const;
};

} // namespace demi::runtime::scripting
