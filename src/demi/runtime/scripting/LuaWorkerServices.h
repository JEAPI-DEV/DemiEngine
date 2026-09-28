#pragma once

namespace demi::runtime {
class LuaScriptHost;

// Snapshot project configuration on the main thread. Each subsequently forked
// task gets its own installer; running tasks retain their original snapshot.
void configureLuaWorkerServices(LuaScriptHost &host);
} // namespace demi::runtime
