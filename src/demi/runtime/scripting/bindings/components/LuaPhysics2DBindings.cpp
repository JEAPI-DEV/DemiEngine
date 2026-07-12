#include "demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaPhysics2DBindingModule::install(LuaScriptHost &host,
                                        lua_State *state) const {
  sol::table physics = sol::state_view(state).create_named_table("Physics2D");
  physics.set_function("overlap_box",
                       [&host](float x, float y, float width, float height,
                               sol::optional<std::string> ignored) {
                         return host.physicsOverlapBox(x, y, width, height,
                                                       ignored.value_or(""));
                       });
  physics.set_function(
      "has_contact",
      [&host](const std::string &id, sol::optional<sol::table> filter) {
        return host.physicsHasContact(id, luaContactFilterFromTable(filter));
      });
  physics.set_function("contacts", [state, &host](const std::string &id) {
    return luaContactsTable(state, host.physicsContacts(id));
  });
}
} // namespace demi::runtime
