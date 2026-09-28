# Lua worker task qualification

Public authoring guidance lives in the website's `tasks`, `database`, and
`tcp-client` templates. This record describes ownership and measurements.

## Current design

- `LuaWorkerTasks` uses the existing `JobSystem` with a lazy, configurable pool
  (two workers by default). Each OS worker owns and reuses one Lua VM. No Lua
  state is concurrently accessed and no instruction hooks are installed.
- `LuaTransfer` marshals plain data as a native graph. Table cycles/aliasing,
  nil roots, integers, binary strings and Data markers survive transfers.
  Captured ordinary tables/functions and unknown userdata/metatables are
  rejected. Explicit shared-map handles cross by native ownership.
- `LuaSharedMap` stores immutable value snapshots behind a recursive mutex.
  Ordinary reads retain a snapshot under lock, then marshal outside the lock.
  Lua critical-section callbacks run under a protected call so errors release
  the lock. Main-thread access never waits; workers can wait cooperatively.
- Worker globals and library tables are fresh per job. Worker table GC
  finalizers are prohibited so Lua code cannot mutate shared data after the
  job is marked complete. Deterministic close handlers remain available.
- Queued cancellation is immediate. Running cancellation changes status to
  `cancelling`; `done()` becomes true only when execution/cleanup has ended.
  Scene unload requests cancellation; host shutdown joins the pool before
  destroying Lua. Uncooperative code can delay shutdown indefinitely.
- Native HTTP/TCP/database operations retain their asynchronous services and
  polled handles. Their internal completion primitive and shared database pool
  remain; the unnecessary Lua completion-token/await surface was removed.

## Worker service access

Worker environments now install thread-safe database, HTTP and TCP bindings,
plus pure data conversion, vector/scalar math and regex facilities. The host
snapshots writable storage configuration at project load; installers capture
native service references, not the live world. Task teardown joins workers
before the borrowed services shut down.

Operation `wait()` uses `AsyncCompletion` condition-variable notifications and
a per-task stop token. There is no polling timer. A wait occupies its Lua worker
slot, but database operations use a separate native pool and networking uses
its own I/O services. Timeout/cancellation ends a wait, not the operation; callers
may cancel the operation explicitly. Native driver shutdown remains cooperative.

`Task.main` uses the shared function/argument transfer code to queue a callback
for the owning gameplay VM. The host dispatches one queue snapshot per update,
without holding queue locks while Lua runs. Scene unload cancels pending calls;
an already executing callback is not rolled back. The caller sleeps until the
result is available or its task is cancelled. Callbacks must be short and must
not wait for a shared lock held by the worker.

Connections and operation userdata do not cross VMs. Create them within a task
and return copied data. Supporting additional native handle types requires an
explicit lifetime/transfer contract, not copying Lua userdata addresses.

### Linux service-access validation, 2026-09-28

Release CLI/runtime/editor rebuilt. Fourteen focused CTest targets passed:
worker tasks/context, main-thread calls, task-host integration, transfer/shared
map, async queue, database service/bindings, HTTP/TCP bindings, Lua scripting,
stub contracts and embedded editor play. Tests include notification wakeup on
completion/cancellation, oversized timeouts, concurrent database connections,
shutdown races, fresh module tables, late project-storage configuration and
SQLite results handed to an actual HUD callback. Data null markers retain their
meaning across VM transfers.

The visible Vulkan `examples/async_tasks` E2E passed with one ongoing calculation
and a second worker doing sequential SQLite waits; HUD actions remained usable.
The example validates without diagnostics; the website Lua-service checker
checked 817 references. These are focused checks, not a full repository or
Android qualification. PHP template validation was unavailable in this shell.

An existing worker-test polling helper could treat the second return value
`"busy"` as a successful predicate. It now evaluates only the first return
value and preserves stack balance; the regression check is explicit.

The single-VM `LuaTaskScheduler`, priority rotation, instruction budgets, and
coroutine restrictions were removed. Game callbacks no longer pump any Lua task
scheduler; their normal coroutine functions are unchanged.

## Release measurements, 2026-09-28

Run `cmake --build --preset linux-release --target demi-lua-worker-benchmarks`,
then `build/linux-release/demi-lua-worker-benchmarks`. Results are medians of five
samples. The arithmetic case sums five million integers. Workers are warmed
before timing VM reuse; submission, result handling and bounded polling waits
are included. No renderer or other benchmark runs concurrently.

Ryzen 7 6800H (16 logical CPUs), system Lua 5.4.9, Linux Release build:

| Workload | Median |
|---|---:|
| Direct arithmetic in the caller VM | 28.64 ms |
| Same arithmetic on one pooled worker | 28.83 ms |
| Two arithmetic jobs on two workers | 31.98 ms |
| Completed-handle `done()` poll | 0.0107 microseconds |

The warmed single-job ratio was 1.007; two jobs were about 1.79 times faster
than serial direct work. These are scoped observations, not guarantees. CPU
frequency and other desktop activity were not controlled. Large copied inputs,
tiny jobs, contention and cold startup have different costs.

For comparison, the retired instruction-sliced implementation measured
30.29 ms direct versus 64.39 ms managed for the same arithmetic workload
(about 2.13 times the CPU time) on September 27. That is historical evidence,
not a measurement of the worker implementation.

## Validation

Focused tests cover graph copies and cycles, rejected captures, two-worker
barriers, main-thread responsiveness, fresh environments on reused VMs,
results/errors/nils, queued and cooperative cancellation, handle lifetime,
non-blocking main-thread map access, atomic increments, scoped locking and
unlocking after Lua errors/yield attempts.

`lua_task_host_tests` exercises real `@HandleAction` dispatch while a worker
runs. `examples/async_tasks` has a visible HUD/SQLite E2E test. The existing
headless app loop does not advance its E2E runner; starting it with
`--e2e-tests` alone is not evidence that the UI test ran.

Linux results do not establish Android device performance or hard-real-time
deadlines. A task is real parallel work, not permission to touch the gameplay
VM, renderer, or live world from another thread.
