#include "demi/runtime/scripting/text/RegexMatcher.h"

#include <iostream>

int main() {
  const demi::runtime::scripting::RegexMatcher matcher;
  if (!matcher.isValid("^(potion|key)$") || matcher.isValid("[") ||
      !matcher.matches("Potion", "^pot") ||
      matcher.matches("Sword", "^(potion|key)$") ||
      matcher.matches("Potion", "^pot", true)) {
    std::cerr << "Regex matcher validation, search, or case handling failed.\n";
    return 1;
  }
  return 0;
}
