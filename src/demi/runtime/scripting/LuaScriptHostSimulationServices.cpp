#include "demi/runtime/scripting/LuaScriptHost.h"

namespace demi::runtime {

void LuaScriptHost::seedRandom(const std::uint64_t seed) { random_.seed(seed); }

std::uint64_t LuaScriptHost::randomState() const { return random_.state(); }

void LuaScriptHost::restoreRandomState(const std::uint64_t state) {
  random_.restore(state);
}

float LuaScriptHost::randomValue() { return random_.value(); }

float LuaScriptHost::randomRange(const float minimum, const float maximum) {
  return random_.range(minimum, maximum);
}

int LuaScriptHost::randomInteger(const int minimum, const int maximum) {
  return random_.integer(minimum, maximum);
}

} // namespace demi::runtime
