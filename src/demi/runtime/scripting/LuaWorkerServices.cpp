#include "demi/runtime/scripting/LuaWorkerServices.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/bindings/LuaDatabaseBindings.h"
#include "demi/runtime/scripting/bindings/LuaHttpBindings.h"
#include "demi/runtime/scripting/bindings/LuaTcpBindings.h"
#include "demi/runtime/scripting/bindings/data/LuaDataBindings.h"
#include "demi/runtime/scripting/bindings/math/LuaVectorMathBindings.h"
#include "demi/runtime/scripting/bindings/text/LuaRegexBindings.h"

namespace demi::runtime {
void configureLuaWorkerServices(LuaScriptHost &host) {
  // The host joins tasks before destroying services. Never capture the live
  // host or application settings in callbacks invoked on a worker thread.
  host.workerTasks().setModuleInstaller(
      [database = &host.databaseService(), http = &host.httpClient(),
       tcp = &host.tcpClient(),
       userDataPath =
           host.applicationServices().userDataPath()](lua_State *worker) {
        installLuaDatabaseBindings(worker, *database, userDataPath);
        installLuaHttpBindings(worker, *http);
        installLuaTcpBindings(worker, *tcp);
        installLuaDataValueBindings(worker);
        installLuaVectorMathBindings(worker);
        installLuaRegexBindings(worker);
      });
}
} // namespace demi::runtime
