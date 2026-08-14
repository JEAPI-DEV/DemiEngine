#include "demi/runtime/scripting/ScriptPropertyContract.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main() {
  using nlohmann::json;
  const json schema = {
      {"speed",
       {{"type", "number"},
        {"default", 6.0},
        {"minimum", 0.0},
        {"maximum", 20.0}}},
      {"lives", {{"type", "integer"}, {"default", 3}}},
      {"team",
       {{"type", "enum"}, {"values", {"red", "blue"}}, {"default", "red"}}},
      {"texture", {{"type", "asset"}, {"required", true}}},
      {"target", {{"type", "entity"}}},
      {"offset", {{"type", "vec2"}, {"default", {0.0, 1.0}}}},
  };

  std::string error;
  auto resolved = demi::runtime::resolveScriptProperties(
      schema,
      {{"speed", 12.5},
       {"team", "blue"},
       {"texture", "asset://characters/player"}},
      error);
  assert(resolved);
  assert((*resolved)["speed"] == 12.5);
  assert((*resolved)["lives"] == 3);
  assert((*resolved)["team"] == "blue");
  assert((*resolved)["offset"] == json({0.0, 1.0}));
  assert(!resolved->contains("target"));

  error.clear();
  assert(!demi::runtime::resolveScriptProperties(
      schema, {{"texture", "asset://ok"}, {"speeed", 3.0}}, error));
  assert(error.find("Unknown script property 'speeed'") != std::string::npos);

  error.clear();
  assert(!demi::runtime::resolveScriptProperties(
      schema, {{"texture", "asset://ok"}, {"speed", 21.0}}, error));
  assert(error.find("above its maximum") != std::string::npos);

  error.clear();
  assert(!demi::runtime::resolveScriptProperties(
      schema, {{"texture", "characters/player"}}, error));
  assert(error.find("must be an asset") != std::string::npos);

  error.clear();
  assert(
      !demi::runtime::resolveScriptProperties(schema, json::object(), error));
  assert(error.find("Required script property 'texture'") != std::string::npos);

  error.clear();
  assert(!demi::runtime::resolveScriptProperties(
      {{"broken", {{"type", "matrix"}}}}, json::object(), error));
  assert(error.find("unsupported type 'matrix'") != std::string::npos);

  return 0;
}
