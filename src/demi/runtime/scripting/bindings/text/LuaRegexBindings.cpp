#include "demi/runtime/scripting/bindings/text/LuaRegexBindings.h"

#include "demi/runtime/scripting/text/RegexMatcher.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaRegexBindingModule::install(LuaScriptHost &, lua_State *state) const {
  const scripting::RegexMatcher matcher;
  sol::table regex = sol::state_view(state).create_named_table("Regex");
  regex.set_function("is_valid", [matcher](const std::string &pattern) {
    return matcher.isValid(pattern);
  });
  regex.set_function(
      "matches", [matcher](const std::string &value, const std::string &pattern,
                           const sol::optional<bool> caseSensitive) {
        return matcher.matches(value, pattern, caseSensitive.value_or(false));
      });
}

} // namespace demi::runtime
