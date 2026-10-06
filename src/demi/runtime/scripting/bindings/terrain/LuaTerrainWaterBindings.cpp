#include "demi/runtime/scripting/bindings/terrain/LuaTerrainWaterBindings.h"

#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/terrain/TerrainWaterTracker.h"
#include <sol/sol.hpp>

#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace {
Vec3 positionFromLua(const sol::table &value) {
  std::size_t count = 0;
  for (const auto &[key, item] : value) {
    if (key.get_type() != sol::type::number)
      throw std::invalid_argument("Water position must be a dense XYZ array");
    const auto index = key.as<double>();
    if (index != 1 && index != 2 && index != 3)
      throw std::invalid_argument("Water position must be a dense XYZ array");
    ++count;
  }
  if (count != 3)
    throw std::invalid_argument("Water position requires three XYZ values");
  float values[3];
  for (int index = 1; index <= 3; ++index) {
    const auto item = value.raw_get<sol::object>(index);
    if (item.get_type() != sol::type::number)
      throw std::invalid_argument(
          "Water position requires finite numeric XYZ values");
    values[index - 1] = item.as<float>();
    if (!std::isfinite(values[index - 1]))
      throw std::invalid_argument(
          "Water position requires finite numeric XYZ values");
  }
  return {values[0], values[1], values[2]};
}
sol::table vectorTable(lua_State *state, Vec3 value) {
  auto table = sol::state_view(state).create_table(3, 0);
  table[1] = value.x;
  table[2] = value.y;
  table[3] = value.z;
  return table;
}
sol::table sampleTable(lua_State *state, const TerrainWaterSample &sample) {
  auto result = sol::state_view(state).create_table();
  result["terrain_id"] = sample.terrainId;
  result["body_id"] = sample.bodyId;
  result["kind"] = sample.kind;
  result["surface"] = vectorTable(state, sample.surface);
  result["normal"] = vectorTable(state, sample.normal);
  result["depth"] = sample.depth;
  result["underwater"] = sample.underwater;
  return result;
}
sol::object optionalSample(lua_State *state,
                           const std::optional<TerrainWaterSample> &sample) {
  return sample ? sol::make_object(state, sampleTable(state, *sample))
                : sol::make_object(state, sol::nil);
}

class LuaWaterTracker {
public:
  LuaWaterTracker(LuaScriptHost &host, std::string terrain)
      : host_(&host), terrain_(std::move(terrain)) {}
  sol::table update(sol::table position, sol::this_state state) {
    const auto events = tracker_.update(
        host_->sampleTerrainWater(positionFromLua(position), terrain_));
    auto result = sol::state_view(state).create_table(int(events.size()), 0);
    int index = 1;
    for (const auto &event : events) {
      auto value = sol::state_view(state).create_table();
      value["phase"] = event.phase;
      value["sample"] = sampleTable(state, event.sample);
      result[index++] = value;
    }
    return result;
  }
  sol::object current(sol::this_state state) const {
    return optionalSample(state, tracker_.current());
  }
  void reset() { tracker_.reset(); }

private:
  LuaScriptHost *host_;
  std::string terrain_;
  TerrainWaterTracker tracker_;
};
} // namespace

void LuaTerrainWaterBindingModule::install(LuaScriptHost &host,
                                           lua_State *state) const {
  sol::state_view lua(state);
  lua.new_usertype<LuaWaterTracker>(
      "WaterTracker", sol::no_constructor, "update", &LuaWaterTracker::update,
      "current", &LuaWaterTracker::current, "reset", &LuaWaterTracker::reset);
  lua["WaterTracker"] = sol::nil;
  auto water = lua.create_named_table("TerrainWater");
  water.set_function(
      "sample",
      [&host, state](sol::table position, sol::optional<std::string> terrain) {
        return optionalSample(state,
                              host.sampleTerrainWater(positionFromLua(position),
                                                      terrain.value_or("")));
      });
  water.set_function("tracker", [&host](sol::optional<std::string> terrain) {
    return std::make_shared<LuaWaterTracker>(host, terrain.value_or(""));
  });
}
} // namespace demi::runtime
