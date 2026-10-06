#include "demi/assets/DataValueRead.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace demi::assets::data_read {
namespace {

// Every decoder takes a LOCATION that is already composed. Composing it in the
// public overloads and then re-composing it in a value-taking overload was
// producing messages like "Terrain preset Terrain preset cells_xcells_x: :",
// because at() was applied twice to the same text.
float numberAt(const DataValue &value, const std::string &location) {
  require(value.isNumber(), location + "must be a number.");
  return value.isInteger()
             ? static_cast<float>(std::get<std::int64_t>(value.value))
             : static_cast<float>(std::get<double>(value.value));
}

int integerAt(const DataValue &value, const std::string &location) {
  require(value.isInteger(), location + "must be an integer.");
  return static_cast<int>(std::get<std::int64_t>(value.value));
}

std::string textAt(const DataValue &value, const std::string &location) {
  require(value.isString(), location + "must be a string.");
  return std::get<std::string>(value.value);
}

std::string join(std::string_view kind, std::string_view where,
                 std::string_view field) {
  std::string prefix;
  prefix += kind;
  prefix += ' ';
  prefix += where;
  if (!field.empty() && field.front() != '/' && where != field)
    prefix += '/';
  prefix += field;
  prefix += ": ";
  return prefix;
}

} // namespace

std::string at(std::string_view kind, std::string_view where) {
  return join(kind, where, {});
}

std::string at(std::string_view kind, std::string_view where,
               std::string_view field) {
  return join(kind, where, field);
}

void reject(std::string message) {
  throw std::invalid_argument(std::move(message));
}

void require(const bool condition, std::string message) {
  if (!condition)
    reject(std::move(message));
}

const DataValue *field(const DataValue &parent, const std::string_view key) {
  return parent.isObject() ? parent.find(key) : nullptr;
}

float number(const DataValue &value, const std::string_view kind,
             const std::string_view where) {
  // The whole reason this function exists: isNumber() covers both
  // representations, so reading only one alternative throws on the other.
  return numberAt(value, at(kind, where));
}

float number(const DataValue &parent, const std::string_view key,
             const std::string_view kind, const std::string_view where,
             const float fallback) {
  const DataValue *value = field(parent, key);
  return value == nullptr || value->isNull()
             ? fallback
             : numberAt(*value, at(kind, where, key));
}

int integer(const DataValue &value, const std::string_view kind,
            const std::string_view where) {
  return integerAt(value, at(kind, where));
}

int integer(const DataValue &parent, const std::string_view key,
            const std::string_view kind, const std::string_view where,
            const int fallback) {
  const DataValue *value = field(parent, key);
  return value == nullptr || value->isNull()
             ? fallback
             : integerAt(*value, at(kind, where, key));
}

bool boolean(const DataValue &parent, const std::string_view key,
             const std::string_view kind, const std::string_view where,
             const bool fallback) {
  const DataValue *value = field(parent, key);
  if (value == nullptr || value->isNull())
    return fallback;
  require(value->isBoolean(), at(kind, where, key) + "must be a boolean.");
  (void)where;
  return std::get<bool>(value->value);
}

std::string text(const DataValue &value, const std::string_view kind,
                 const std::string_view where) {
  return textAt(value, at(kind, where));
}

std::string text(const DataValue &parent, const std::string_view key,
                 const std::string_view kind, const std::string_view where,
                 const bool required, std::string fallback) {
  const DataValue *value = field(parent, key);
  if (value == nullptr || value->isNull()) {
    require(!required, at(kind, where, key) + "is required.");
    return fallback;
  }
  const auto location = at(kind, where, key);
  auto result = textAt(*value, location);
  if (required)
    require(!result.empty(), location + "must not be empty.");
  return result;
}

float ranged(const DataValue &parent, const std::string_view key,
             const std::string_view kind, const std::string_view where,
             const float minimum, const float maximum, const float fallback) {
  const float parsed = number(parent, key, kind, where, fallback);
  if (parsed < minimum || parsed > maximum)
    reject(at(kind, where, key) + "must be between " + readable(minimum) +
          " and " + readable(maximum) + ", but is " + readable(parsed) + ".");
  return parsed;
}

int rangedInt(const DataValue &parent, const std::string_view key,
              const std::string_view kind, const std::string_view where,
              const int minimum, const int maximum, const int fallback) {
  const int parsed = integer(parent, key, kind, where, fallback);
  if (parsed < minimum || parsed > maximum)
    reject(at(kind, where, key) + "must be between " + readable(
              static_cast<float>(minimum)) +
          " and " + readable(static_cast<float>(maximum)) + ", but is " +
          std::to_string(parsed) + ".");
  return parsed;
}

runtime::Color color(const DataValue &value, const std::string_view kind,
            const std::string_view where, const runtime::Color fallback) {
  const DataValue::Array *channels = value.array();
  require(channels != nullptr && channels->size() == 4,
          at(kind, where) + "must be an [r, g, b, a] quad.");
  runtime::Color result = fallback;
  float *slots[] = {&result.r, &result.g, &result.b, &result.a};
  for (std::size_t index = 0; index < 4; ++index) {
    const float channel = number((*channels)[index], kind, where);
    // Linear channels; a value outside 0 to 1 is a broken conversion rather
    // than a look the shading pipeline can reproduce.
    require(channel >= 0.F && channel <= 1.F,
            at(kind, where) + "channels must be within [0, 1], but one is " +
                readable(channel) + ".");
    *slots[index] = channel;
  }
  return result;
}

runtime::Vec2 vec2(const DataValue &value, const std::string_view kind,
                   const std::string_view where) {
  const DataValue::Array *entries = value.array();
  require(entries != nullptr && entries->size() == 2,
          at(kind, where) + "must be a [x, z] pair.");
  return {number((*entries)[0], kind, where), number((*entries)[1], kind, where)};
}

void fields(const DataValue &value, const std::string_view kind,
            const std::string_view where,
            const std::initializer_list<std::string_view> allowed) {
  requireObject(value, kind, where);
  for (const auto &[key, unused] : *value.object()) {
    (void)unused;
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      reject(at(kind, where, key) + "is not a known field.");
  }
}

void requireObject(const DataValue &value, const std::string_view kind,
                   const std::string_view where) {
  require(value.isObject(), at(kind, where) + "must be an object.");
}

void requireArray(const DataValue &value, const std::string_view kind,
                  const std::string_view where) {
  require(value.isArray(), at(kind, where) + "must be an array.");
}

void requireNonEmpty(const DataValue &value, const std::string_view kind,
                     const std::string_view where) {
  require(value.isObject() && !value.object()->empty(),
          at(kind, where) + "must be a non-empty object.");
}

std::string readable(const float value) {
  if (std::isfinite(value) && value == std::floor(value) &&
      std::abs(value) < 1e6F)
    return std::to_string(static_cast<long long>(value));
  std::ostringstream text;
  text << std::fixed << std::setprecision(2) << value;
  return text.str();
}

} // namespace demi::assets::data_read
