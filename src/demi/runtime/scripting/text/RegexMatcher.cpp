#include "demi/runtime/scripting/text/RegexMatcher.h"

#include <regex>
#include <string>

namespace demi::runtime::scripting {
namespace {

std::regex makeRegex(const std::string_view pattern, const bool caseSensitive) {
  auto flags = std::regex_constants::ECMAScript;
  if (!caseSensitive)
    flags |= std::regex_constants::icase;
  return std::regex(std::string(pattern), flags);
}

} // namespace

bool RegexMatcher::isValid(const std::string_view pattern) const {
  try {
    (void)makeRegex(pattern, true);
    return true;
  } catch (const std::regex_error &) {
    return false;
  }
}

bool RegexMatcher::matches(const std::string_view value,
                           const std::string_view pattern,
                           const bool caseSensitive) const {
  try {
    return std::regex_search(std::string(value),
                             makeRegex(pattern, caseSensitive));
  } catch (const std::regex_error &) {
    return false;
  }
}

} // namespace demi::runtime::scripting
