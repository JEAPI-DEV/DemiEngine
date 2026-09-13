#pragma once

struct lua_State;

namespace demi::runtime {

class GameNetworkSession;
class LuaScriptHost;
class NetworkQueryHistory2D;

void installNetworkQueryHistoryBindings(LuaScriptHost &host, lua_State *state,
                                        GameNetworkSession &session,
                                        NetworkQueryHistory2D &history);

} // namespace demi::runtime
