#include "demi/runtime/scripting/LuaWorkerTasks.h"
#include "demi/runtime/scripting/LuaSharedMap.h"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void run(lua_State *state, const char *source) {
  if (luaL_dostring(state, source) != LUA_OK)
    throw std::runtime_error(lua_tostring(state, -1));
}
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
template<class Function> double median(Function function) {
  std::vector<double> measurements;
  for (int sample = 0; sample < 5; ++sample) measurements.push_back(function());
  std::sort(measurements.begin(), measurements.end());
  return measurements[2];
}
void wait(const std::shared_ptr<demi::runtime::LuaWorkerTask> &task) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!task->done() && Clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  if (!task->done()) throw std::runtime_error("Benchmark task timeout");
  if (task->status() != demi::runtime::LuaWorkerTaskStatus::Completed)
    throw std::runtime_error(task->error());
}
}

int main() {
  using namespace demi::runtime;
  auto *state = luaL_newstate();
  luaL_openlibs(state);
  LuaWorkerTasks workers;
  workers.attach(state);
  installLuaSharedBindings(state);
  installLuaTaskBindings(state, workers);
  try {
    run(state, "function calculate(n) local sum=0; for i=1,n do sum=sum+i end; return sum end");
    const auto submit = [&] {
      lua_getglobal(state, "calculate");
      lua_pushinteger(state, 5000000);
      auto task = workers.fork(state, -2, -1, 1);
      lua_pop(state, 2);
      return task;
    };
    const double direct = median([&] {
      const auto start = Clock::now();
      run(state, "assert(calculate(5000000)==12500002500000)");
      return elapsed(start);
    });
    auto first = submit();
    auto second = submit();
    wait(first); wait(second); // Warm both worker VMs before timing reuse.
    const double worker = median([&] {
      const auto start = Clock::now();
      auto task = submit();
      wait(task);
      const double duration = elapsed(start);
      task->pushResult(state);
      if (lua_tointeger(state, -1) != 12500002500000LL) throw std::runtime_error("Wrong result");
      lua_pop(state, 1);
      return duration;
    });
    const double parallel = median([&] {
      const auto start = Clock::now();
      auto a = submit(); auto b = submit();
      wait(a); wait(b);
      return elapsed(start);
    });
    const double polling = median([&] {
      const auto start = Clock::now();
      for (int i = 0; i < 100000; ++i) (void)first->done();
      return elapsed(start) * 1000 / 100000;
    });
    std::cout << std::fixed << std::setprecision(4)
              << "direct_ms=" << direct << '\n'
              << "worker_ms=" << worker << '\n'
              << "worker_direct_ratio=" << worker / direct << '\n'
              << "two_workers_ms=" << parallel << '\n'
              << "two_jobs_vs_serial_speedup=" << direct * 2 / parallel << '\n'
              << "done_poll_us=" << polling << '\n';
    workers.shutdown();
    lua_close(state);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    workers.shutdown();
    lua_close(state);
    return 1;
  }
}
