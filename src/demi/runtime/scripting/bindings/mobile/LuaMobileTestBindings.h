#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

// Mobile end-to-end test API. Tests run as coroutines driven by the runtime
// and interact with the game through semantic input actions:
//
//   Mobile.touch("menu_button_levels")
//   Mobile.swipe("list_top", "list_bottom", 0.4)
//   Mobile.wait(1.5)
//   Mobile.expect_scene("scene://game/main", 10.0)
//   Mobile.expect(condition, "message")
//
// Touches resolve nodes by id through the live HUD layout and inject
// synthetic fingers into the same input state real fingers use, so tests
// exercise the full input pipeline on device.
class LuaMobileTestBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
