#include "demi/runtime/scripting/LuaScriptHost.h"

namespace demi::runtime {
std::optional<TerrainWaterSample>
LuaScriptHost::sampleTerrainWater(Vec3 position, std::string_view terrainId) {
  return world_ ? terrainWaterRuntime_.sample(*world_, position, terrainId)
                : std::nullopt;
}
} // namespace demi::runtime
