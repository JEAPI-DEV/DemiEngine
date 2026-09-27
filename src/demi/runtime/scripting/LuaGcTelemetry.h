#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace demi::runtime {

// Counters and sampled peak belong to the Lua VM, including across profiler
// session resets. They end when that VM is closed.
struct LuaGcSnapshot {
  std::size_t heapBytes = 0;
  std::size_t heapPeakSampledBytes = 0;
  bool isGcRunning = false;
  std::uint64_t collectRequests = 0;
  std::uint64_t stepRequests = 0;
  std::uint64_t stepCycles = 0;
};

void installLuaGcTelemetry(lua_State *state);
[[nodiscard]] LuaGcSnapshot sampleLuaGcTelemetry(lua_State *state);
void recordLuaGcTelemetryFrame(lua_State *state);
void collectLuaGarbage(lua_State *state);

} // namespace demi::runtime
