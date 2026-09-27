#include "demi/runtime/scripting/LuaGcTelemetry.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"

extern "C" {
#include <lua.h>
}

#include <chrono>
#include <cstring>
#include <memory>

namespace demi::runtime {
namespace {

char telemetryRegistryKey;

struct VmTelemetry {
  std::size_t sampledPeakBytes = 0;
  std::uint64_t collectRequests = 0;
  std::uint64_t stepRequests = 0;
  std::uint64_t stepCycles = 0;
};

VmTelemetry *telemetry(lua_State *state) {
  lua_rawgetp(state, LUA_REGISTRYINDEX, &telemetryRegistryKey);
  auto *result = static_cast<VmTelemetry *>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return result;
}

std::size_t heapBytes(lua_State *state) {
  const auto kilobytes = static_cast<std::size_t>(lua_gc(state, LUA_GCCOUNT));
  const auto remainder = static_cast<std::size_t>(lua_gc(state, LUA_GCCOUNTB));
  return kilobytes * 1024 + remainder;
}

void recordExplicitCollection(
    const std::chrono::steady_clock::time_point start) {
  if (!RuntimeProfiler::enabled())
    return;
  const auto elapsed = std::chrono::steady_clock::now() - start;
  RuntimeProfiler::record(
      "Lua.gc_explicit",
      std::chrono::duration<double, std::milli>(elapsed).count());
}

int collectgarbageWithTelemetry(lua_State *state) {
  VmTelemetry *data = telemetry(state);
  const int argumentCount = lua_gettop(state);
  const int optionType = lua_type(state, 1);
  const char *option =
      optionType == LUA_TSTRING ? lua_tostring(state, 1) : nullptr;
  const bool isCollect =
      optionType == LUA_TNONE || optionType == LUA_TNIL ||
      (option != nullptr && std::strcmp(option, "collect") == 0);
  const bool isStep = option != nullptr && std::strcmp(option, "step") == 0;
  if (data != nullptr) {
    if (isCollect)
      ++data->collectRequests;
    if (isStep)
      ++data->stepRequests;
  }

  // Calling the original under pcall preserves its return values and Lua error
  // object. lua_error runs only after the C++ timing work has returned.
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  const auto start = std::chrono::steady_clock::now();
  const int status = lua_pcall(state, argumentCount, LUA_MULTRET, 0);
  if (isCollect || isStep)
    recordExplicitCollection(start);
  if (status != LUA_OK)
    return lua_error(state);
  if (isStep && data != nullptr && lua_toboolean(state, 1))
    ++data->stepCycles;
  return lua_gettop(state);
}

} // namespace

void installLuaGcTelemetry(lua_State *state) {
  if (telemetry(state) != nullptr)
    return;
  lua_getglobal(state, "collectgarbage");
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  std::construct_at(static_cast<VmTelemetry *>(
      lua_newuserdatauv(state, sizeof(VmTelemetry), 0)));
  lua_rawsetp(state, LUA_REGISTRYINDEX, &telemetryRegistryKey);
  lua_pushcclosure(state, collectgarbageWithTelemetry, 1);
  lua_setglobal(state, "collectgarbage");
  (void)sampleLuaGcTelemetry(state);
}

LuaGcSnapshot sampleLuaGcTelemetry(lua_State *state) {
  LuaGcSnapshot result;
  result.heapBytes = heapBytes(state);
  result.isGcRunning = lua_gc(state, LUA_GCISRUNNING) != 0;
  if (VmTelemetry *data = telemetry(state)) {
    if (result.heapBytes > data->sampledPeakBytes)
      data->sampledPeakBytes = result.heapBytes;
    result.heapPeakSampledBytes = data->sampledPeakBytes;
    result.collectRequests = data->collectRequests;
    result.stepRequests = data->stepRequests;
    result.stepCycles = data->stepCycles;
  } else {
    result.heapPeakSampledBytes = result.heapBytes;
  }
  return result;
}

void recordLuaGcTelemetryFrame(lua_State *state) {
  const LuaGcSnapshot snapshot = sampleLuaGcTelemetry(state);
  if (!RuntimeProfiler::enabled())
    return;
  RuntimeProfiler::setGauge("Lua.heap_bytes",
                            static_cast<double>(snapshot.heapBytes));
  RuntimeProfiler::setGauge("Lua.heap_peak_sampled_bytes",
                            static_cast<double>(snapshot.heapPeakSampledBytes));
  RuntimeProfiler::setGauge("Lua.gc_running", snapshot.isGcRunning ? 1.0 : 0.0);
  RuntimeProfiler::setGauge("Lua.gc_collect_requests",
                            static_cast<double>(snapshot.collectRequests));
  RuntimeProfiler::setGauge("Lua.gc_step_requests",
                            static_cast<double>(snapshot.stepRequests));
  RuntimeProfiler::setGauge("Lua.gc_step_cycles",
                            static_cast<double>(snapshot.stepCycles));
}

void collectLuaGarbage(lua_State *state) {
  if (VmTelemetry *data = telemetry(state))
    ++data->collectRequests;
  const auto start = std::chrono::steady_clock::now();
  lua_gc(state, LUA_GCCOLLECT, 0);
  recordExplicitCollection(start);
}

} // namespace demi::runtime
