#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct lua_State;

namespace demi::runtime {

class LuaSharedMap;

// No Lua references or recursive native ownership: table edges are indices.
struct LuaTransferGraph {
  struct Value {
    enum class Kind { Nil, Boolean, Integer, Number, String, Table, Shared };
    Kind kind = Kind::Nil;
    bool boolean = false;
    std::int64_t integer = 0;
    double number = 0;
    std::string string;
    std::size_t table = 0;
    std::shared_ptr<LuaSharedMap> shared;
  };

  struct Table {
    enum class Marker { None, Array, Object, Null };
    Marker marker = Marker::None;
    std::vector<std::pair<Value, Value>> entries;
  };

  std::vector<Value> roots;
  std::vector<Table> tables;
};

// Capture preserves the stack. Negative first indices refer to the original
// stack; count includes nil roots. Set allowShared=false for SharedMap storage
// to prevent native shared-ownership cycles, including through nested tables.
// Unsupported values/metatables and malformed graphs throw invalid_argument.
[[nodiscard]] LuaTransferGraph captureLuaValues(lua_State *state, int first,
                                                int count,
                                                bool allowShared = true);
// Push appends exactly roots.size() values; failure restores the original
// stack.
void pushLuaValues(lua_State *state, const LuaTransferGraph &graph);

} // namespace demi::runtime
