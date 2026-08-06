#include "demi/runtime/scripting/bindings/data/LuaDataBindings.h"

#include "demi/assets/DataDocument.h"

#include <algorithm>

extern "C" {
#include <lauxlib.h>
}

namespace demi::runtime {
namespace {

LuaScriptHost &host(lua_State *state) {
  return *static_cast<LuaScriptHost *>(
      lua_touserdata(state, lua_upvalueindex(1)));
}

void setKind(lua_State *state, const char *kind) {
  lua_newtable(state);
  lua_pushstring(state, kind);
  lua_setfield(state, -2, "__demi_json_kind");
  lua_pushboolean(state, false);
  lua_setfield(state, -2, "__metatable");
  lua_setmetatable(state, -2);
}

void pushDataValue(lua_State *state, const assets::DataValue &value) {
  if (value.isNull()) {
    lua_getglobal(state, "Data");
    lua_getfield(state, -1, "null");
    lua_remove(state, -2);
    return;
  }
  if (value.isBoolean()) {
    lua_pushboolean(state, std::get<bool>(value.value));
    return;
  }
  if (value.isInteger()) {
    lua_pushinteger(state, std::get<std::int64_t>(value.value));
    return;
  }
  if (std::holds_alternative<double>(value.value)) {
    lua_pushnumber(state, std::get<double>(value.value));
    return;
  }
  if (value.isString()) {
    const std::string &text = std::get<std::string>(value.value);
    lua_pushlstring(state, text.data(), text.size());
    return;
  }
  lua_newtable(state);
  if (const auto *array = value.array()) {
    for (std::size_t index = 0; index < array->size(); ++index) {
      pushDataValue(state, (*array)[index]);
      lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    setKind(state, "array");
    return;
  }
  if (const auto *object = value.object()) {
    for (const auto &[key, child] : *object) {
      pushDataValue(state, child);
      lua_setfield(state, -2, key.c_str());
    }
  }
  setKind(state, "object");
}

void pushError(lua_State *state, const char *code, std::string message,
               std::string path = {}) {
  lua_newtable(state);
  lua_pushstring(state, code);
  lua_setfield(state, -2, "code");
  lua_pushlstring(state, message.data(), message.size());
  lua_setfield(state, -2, "message");
  lua_pushlstring(state, path.data(), path.size());
  lua_setfield(state, -2, "path");
}

int load(lua_State *state) {
  const char *id = luaL_checkstring(state, 1);
  const auto snapshot = host(state).dataAssetStore().load(id);
  if (!snapshot) {
    lua_pushnil(state);
    pushError(state, "DATA_ASSET_NOT_FOUND",
              "Data asset is not loaded: " + std::string(id), id);
    return 2;
  }
  pushDataValue(state, snapshot->document->root());
  lua_pushnil(state);
  return 2;
}

int revision(lua_State *state) {
  const char *id = luaL_checkstring(state, 1);
  lua_pushinteger(state, static_cast<lua_Integer>(
                             host(state).dataAssetStore().revision(id)));
  return 1;
}

int query(lua_State *state) {
  DataAssetQuery filter;
  if (lua_istable(state, 1)) {
    lua_getfield(state, 1, "content_type");
    if (lua_isstring(state, -1))
      filter.contentType = lua_tostring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 1, "tags");
    if (lua_istable(state, -1)) {
      const lua_Integer count = luaL_len(state, -1);
      for (lua_Integer index = 1; index <= count; ++index) {
        lua_rawgeti(state, -1, index);
        if (lua_isstring(state, -1))
          filter.tags.emplace_back(lua_tostring(state, -1));
        lua_pop(state, 1);
      }
    }
    lua_pop(state, 1);
  }
  const auto snapshots = host(state).dataAssetStore().query(std::move(filter));
  lua_createtable(state, static_cast<int>(snapshots.size()), 0);
  for (std::size_t index = 0; index < snapshots.size(); ++index) {
    pushDataValue(state, snapshots[index]->document->root());
    lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
  }
  setKind(state, "array");
  return 1;
}

int kind(lua_State *state) {
  lua_getglobal(state, "Data");
  lua_getfield(state, -1, "null");
  const bool isNull = lua_rawequal(state, 1, -1);
  lua_pop(state, 2);
  if (isNull) {
    lua_pushliteral(state, "null");
    return 1;
  }
  if (!lua_istable(state, 1) || !lua_getmetatable(state, 1)) {
    lua_pushnil(state);
    return 1;
  }
  lua_getfield(state, -1, "__demi_json_kind");
  lua_remove(state, -2);
  return 1;
}

int isNull(lua_State *state) {
  lua_getglobal(state, "Data");
  lua_getfield(state, -1, "null");
  const bool result = lua_rawequal(state, 1, -1);
  lua_pop(state, 2);
  lua_pushboolean(state, result);
  return 1;
}

void addFunction(lua_State *state, const char *name, lua_CFunction function,
                 LuaScriptHost &scriptHost) {
  lua_pushlightuserdata(state, &scriptHost);
  lua_pushcclosure(state, function, 1);
  lua_setfield(state, -2, name);
}

} // namespace

void LuaDataBindingModule::install(LuaScriptHost &scriptHost,
                                   lua_State *state) const {
  lua_newtable(state);
  addFunction(state, "load", load, scriptHost);
  addFunction(state, "query", query, scriptHost);
  addFunction(state, "revision", revision, scriptHost);
  addFunction(state, "kind", kind, scriptHost);
  addFunction(state, "is_null", isNull, scriptHost);
  lua_newtable(state);
  lua_newtable(state);
  lua_pushliteral(state, "null");
  lua_setfield(state, -2, "__demi_json_kind");
  lua_pushboolean(state, false);
  lua_setfield(state, -2, "__metatable");
  lua_setmetatable(state, -2);
  lua_setfield(state, -2, "null");
  lua_setglobal(state, "Data");
}

} // namespace demi::runtime
