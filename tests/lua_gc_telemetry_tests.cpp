#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/LuaGcTelemetry.h"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

using demi::runtime::LuaGcSnapshot;
using demi::runtime::RuntimeProfiler;

bool run(lua_State *state, const char *source) {
  if (luaL_dostring(state, source) == LUA_OK)
    return true;
  std::cerr << lua_tostring(state, -1) << '\n';
  lua_pop(state, 1);
  return false;
}

bool gaugeEquals(std::string_view name, double expected) {
  for (const auto &entry : RuntimeProfiler::frameEntries()) {
    if (entry.name == name)
      return entry.hasGauge && entry.gauge == expected;
  }
  return false;
}

bool testTelemetry() {
  std::unique_ptr<lua_State, decltype(&lua_close)> first(luaL_newstate(),
                                                         lua_close);
  std::unique_ptr<lua_State, decltype(&lua_close)> second(luaL_newstate(),
                                                          lua_close);
  if (!first || !second)
    return false;
  luaL_openlibs(first.get());
  luaL_openlibs(second.get());
  if (!run(first.get(), "stock_collectgarbage = collectgarbage"))
    return false;
  demi::runtime::installLuaGcTelemetry(first.get());
  demi::runtime::installLuaGcTelemetry(first.get());
  demi::runtime::installLuaGcTelemetry(second.get());

  const LuaGcSnapshot initial =
      demi::runtime::sampleLuaGcTelemetry(first.get());
  if (initial.heapBytes == 0 ||
      initial.heapPeakSampledBytes < initial.heapBytes ||
      !initial.isGcRunning || initial.collectRequests != 0 ||
      initial.stepRequests != 0 || initial.stepCycles != 0)
    return false;

  if (!run(first.get(), R"lua(
    assert(collectgarbage() == 0)
    assert(collectgarbage('collect') == 0)
    assert(collectgarbage(nil) == 0)
    assert(type(collectgarbage('count')) == 'number')
    assert(type(collectgarbage('step', 1)) == 'boolean')
    assert(type(collectgarbage('step')) == 'boolean')
    assert(collectgarbage('isrunning') == stock_collectgarbage('isrunning'))
    local old = stock_collectgarbage('setpause', 176)
    assert(collectgarbage('setpause', old) == 176)
    local ok, err = pcall(collectgarbage, 'unknown')
    assert(not ok and type(err) == 'string' and err:find('invalid option'))
    ok, err = pcall(collectgarbage, 'step', {})
    assert(not ok and type(err) == 'string' and err:find('number expected'))
    ok, err = pcall(collectgarbage, false)
    assert(not ok and type(err) == 'string')
    local fiber = coroutine.create(function()
      assert(collectgarbage('collect') == 0)
      return collectgarbage('step', 1)
    end)
    local resumed, completed = coroutine.resume(fiber)
    assert(resumed and type(completed) == 'boolean')
    collectgarbage('stop')
    assert(collectgarbage('isrunning') == false)
  )lua"))
    return false;

  LuaGcSnapshot sample = demi::runtime::sampleLuaGcTelemetry(first.get());
  if (sample.collectRequests != 4 || sample.stepRequests != 4 ||
      sample.stepCycles > 3 || sample.isGcRunning)
    return false;

  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::beginFrame();
  demi::runtime::recordLuaGcTelemetryFrame(first.get());
  if (!gaugeEquals("Lua.heap_bytes", static_cast<double>(sample.heapBytes)) ||
      !gaugeEquals("Lua.heap_peak_sampled_bytes",
                   static_cast<double>(sample.heapPeakSampledBytes)) ||
      !gaugeEquals("Lua.gc_running", 0) ||
      !gaugeEquals("Lua.gc_collect_requests", 4) ||
      !gaugeEquals("Lua.gc_step_requests", 4) ||
      !gaugeEquals("Lua.gc_step_cycles",
                   static_cast<double>(sample.stepCycles)))
    return false;

  if (!run(first.get(), R"lua(
    collectgarbage('restart')
    assert(collectgarbage('isrunning'))
    held = string.rep('x', 256 * 1024)
  )lua"))
    return false;
  const LuaGcSnapshot expanded =
      demi::runtime::sampleLuaGcTelemetry(first.get());
  if (!expanded.isGcRunning || expanded.heapBytes <= sample.heapBytes ||
      expanded.heapPeakSampledBytes < expanded.heapBytes)
    return false;
  if (!run(first.get(), "held = nil; collectgarbage('collect')"))
    return false;
  const LuaGcSnapshot reduced =
      demi::runtime::sampleLuaGcTelemetry(first.get());
  if (reduced.heapBytes >= expanded.heapBytes ||
      reduced.heapPeakSampledBytes != expanded.heapPeakSampledBytes ||
      reduced.collectRequests != 5)
    return false;

  RuntimeProfiler::resetSession();
  RuntimeProfiler::beginFrame();
  demi::runtime::recordLuaGcTelemetryFrame(first.get());
  if (!gaugeEquals("Lua.gc_collect_requests", 5))
    return false;
  demi::runtime::collectLuaGarbage(first.get());
  const LuaGcSnapshot afterEngineCollect =
      demi::runtime::sampleLuaGcTelemetry(first.get());
  if (afterEngineCollect.collectRequests != 6)
    return false;
  bool hasCollectionTiming = false;
  for (const auto &entry : RuntimeProfiler::frameEntries()) {
    if (entry.name == "Lua.gc_explicit")
      hasCollectionTiming = entry.calls == 1 &&
                            std::isfinite(entry.totalMilliseconds) &&
                            entry.totalMilliseconds >= 0.0;
  }
  RuntimeProfiler::setEnabled(false);
  if (!hasCollectionTiming)
    return false;

  const LuaGcSnapshot untouched =
      demi::runtime::sampleLuaGcTelemetry(second.get());
  if (untouched.collectRequests != 0 || untouched.stepRequests != 0 ||
      untouched.stepCycles != 0 || untouched.heapBytes == 0)
    return false;
  return run(second.get(), "assert(collectgarbage('collect') == 0)") &&
         demi::runtime::sampleLuaGcTelemetry(second.get()).collectRequests ==
             1 &&
         demi::runtime::sampleLuaGcTelemetry(first.get()).collectRequests == 6;
}

} // namespace

int main() {
  if (!testTelemetry()) {
    std::cerr << "Lua GC telemetry test failed\n";
    return 1;
  }
  std::cout << "Lua GC telemetry tests passed\n";
  return 0;
}
