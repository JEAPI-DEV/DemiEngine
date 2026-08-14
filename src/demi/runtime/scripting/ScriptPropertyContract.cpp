#include "demi/runtime/scripting/ScriptPropertyContract.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime {
namespace {

bool isNumberArray(const nlohmann::json &value, const std::size_t size) {
  if (!value.is_array() || value.size() != size)
    return false;
  for (const auto &item : value)
    if (!item.is_number())
      return false;
  return true;
}

bool validateType(const std::string &name, const std::string &type,
                  const nlohmann::json &value, std::string &error) {
  bool valid = false;
  if (type == "boolean")
    valid = value.is_boolean();
  else if (type == "number")
    valid = value.is_number();
  else if (type == "integer")
    valid = value.is_number() &&
            std::floor(value.get<double>()) == value.get<double>();
  else if (type == "string" || type == "entity")
    valid = value.is_string() && (type != "entity" || !value.empty());
  else if (type == "asset")
    valid = value.is_string() &&
            value.get_ref<const std::string &>().starts_with("asset://");
  else if (type == "array")
    valid = value.is_array();
  else if (type == "object")
    valid = value.is_object();
  else if (type == "vec2")
    valid = isNumberArray(value, 2);
  else if (type == "vec3")
    valid = isNumberArray(value, 3);
  else if (type == "color")
    valid = isNumberArray(value, 4);
  else if (type == "enum")
    valid = value.is_string();
  else {
    error = "Property '" + name + "' has unsupported type '" + type + "'.";
    return false;
  }

  if (!valid) {
    const std::string article =
        type == "asset" || type == "integer" ? "an " : "a ";
    error = "Property '" + name + "' must be " + article + type + ".";
  }
  return valid;
}

bool validateSchemaEntry(const std::string &name,
                         const nlohmann::json &definition, std::string &error) {
  if (!definition.is_object()) {
    error = "Property schema entry '" + name + "' must be an object.";
    return false;
  }
  const std::string type = definition.value("type", std::string{});
  constexpr std::array supportedTypes{"boolean", "number", "integer", "string",
                                      "entity",  "asset",  "array",   "object",
                                      "vec2",    "vec3",   "color",   "enum"};
  if (std::ranges::find(supportedTypes, type) == supportedTypes.end()) {
    error =
        type.empty()
            ? "Property schema entry '" + name + "' requires a type."
            : "Property '" + name + "' has unsupported type '" + type + "'.";
    return false;
  }
  if (type == "enum") {
    const auto values = definition.find("values");
    if (values == definition.end() || !values->is_array() || values->empty() ||
        !std::ranges::all_of(
            *values, [](const auto &value) { return value.is_string(); })) {
      error = "Enum property '" + name + "' requires non-empty string values.";
      return false;
    }
  }
  return true;
}

bool validateDefinition(const std::string &name,
                        const nlohmann::json &definition,
                        const nlohmann::json &value, std::string &error) {
  if (!validateSchemaEntry(name, definition, error))
    return false;
  const std::string type = definition.value("type", std::string{});
  if (!validateType(name, type, value, error))
    return false;

  if (type == "enum") {
    const auto values = definition.find("values");
    if (std::ranges::find(*values, value) == values->end()) {
      error = "Property '" + name + "' is not one of its declared values.";
      return false;
    }
  }

  if (value.is_number()) {
    const double number = value.get<double>();
    if (const auto minimum = definition.find("minimum");
        minimum != definition.end() &&
        (!minimum->is_number() || number < minimum->get<double>())) {
      error = "Property '" + name + "' is below its minimum.";
      return false;
    }
    if (const auto maximum = definition.find("maximum");
        maximum != definition.end() &&
        (!maximum->is_number() || number > maximum->get<double>())) {
      error = "Property '" + name + "' is above its maximum.";
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<nlohmann::json>
resolveScriptProperties(const nlohmann::json &schema,
                        const nlohmann::json &authored, std::string &error) {
  if (!schema.is_object()) {
    error = "property_schema must be a table keyed by property name.";
    return std::nullopt;
  }
  if (!authored.is_object()) {
    error = "LuaScript properties must be an object.";
    return std::nullopt;
  }

  for (const auto &[name, unused] : authored.items()) {
    if (!schema.contains(name)) {
      error = "Unknown script property '" + name + "'.";
      return std::nullopt;
    }
  }

  nlohmann::json resolved = nlohmann::json::object();
  for (const auto &[name, definition] : schema.items()) {
    if (!validateSchemaEntry(name, definition, error))
      return std::nullopt;
    const auto authoredValue = authored.find(name);
    const auto defaultValue = definition.find("default");
    if (authoredValue == authored.end() && defaultValue == definition.end()) {
      if (definition.is_object() && definition.value("required", false)) {
        error = "Required script property '" + name + "' is missing.";
        return std::nullopt;
      }
      continue;
    }

    nlohmann::json value =
        authoredValue != authored.end() ? *authoredValue : *defaultValue;
    if (definition.value("type", std::string{}) == "array" &&
        value.is_object() && value.empty())
      value = nlohmann::json::array();
    if (!validateDefinition(name, definition, value, error))
      return std::nullopt;
    resolved[name] = value;
  }
  return resolved;
}

} // namespace demi::runtime
