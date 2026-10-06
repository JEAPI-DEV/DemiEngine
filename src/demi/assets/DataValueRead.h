#pragma once

#include "demi/assets/DataDocument.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

// Readers for a validated data document.
//
// Every authored document in the engine decodes the same handful of JSON types
// out of a DataValue, and every one of those decoders has the same trap: a JSON
// number is stored as EITHER std::int64_t or double, DataValue::isNumber() is
// true for both, and reading the wrong alternative throws std::bad_variant_access.
// That trap has already produced a real bug, so the handling lives here once
// rather than in each document's parser.
//
// The rules are shared too: a rejection names the document location, a required
// field says so by name, and an empty required string is rejected rather than
// accepted as absent.
namespace demi::assets::data_read {

// "kind where: ", the prefix every rejection carries.
[[nodiscard]] std::string at(std::string_view kind, std::string_view where);
[[nodiscard]] std::string at(std::string_view kind, std::string_view where,
                              std::string_view field);

[[noreturn]] void reject(std::string message);

void require(bool condition, std::string message);

const DataValue *field(const DataValue &parent, std::string_view key);

// Numeric. A JSON integer is not silently widened at the call site; it is
// widened here, once, which is the only place that can get it right.
[[nodiscard]] float number(const DataValue &value, std::string_view kind,
                           std::string_view where);
[[nodiscard]] float number(const DataValue &parent, std::string_view key,
                           std::string_view kind, std::string_view where,
                           float fallback);
[[nodiscard]] int integer(const DataValue &value, std::string_view kind,
                          std::string_view where);
[[nodiscard]] int integer(const DataValue &parent, std::string_view key,
                          std::string_view kind, std::string_view where,
                          int fallback);
[[nodiscard]] bool boolean(const DataValue &parent, std::string_view key,
                           std::string_view kind, std::string_view where,
                           bool fallback);
[[nodiscard]] std::string text(const DataValue &value, std::string_view kind,
                               std::string_view where);
[[nodiscard]] std::string text(const DataValue &parent, std::string_view key,
                               std::string_view kind, std::string_view where,
                               bool required, std::string fallback = {});

// Range-checked scalars. The message names the range in whole numbers where it
// is a whole number, because a rejection is read to fix a document.
[[nodiscard]] float ranged(const DataValue &parent, std::string_view key,
                           std::string_view kind, std::string_view where,
                           float minimum, float maximum, float fallback);
[[nodiscard]] int rangedInt(const DataValue &parent, std::string_view key,
                            std::string_view kind, std::string_view where,
                            int minimum, int maximum, int fallback);

[[nodiscard]] runtime::Color color(const DataValue &value, std::string_view kind,
                          std::string_view where, runtime::Color fallback);
[[nodiscard]] runtime::Vec2 vec2(const DataValue &value, std::string_view kind,
                                  std::string_view where);

// Rejects any key the document does not define, so a typo is an error instead
// of a setting that silently does nothing.
void fields(const DataValue &value, std::string_view kind,
            std::string_view where, std::initializer_list<std::string_view> allowed);

void requireObject(const DataValue &value, std::string_view kind,
                   std::string_view where);
void requireArray(const DataValue &value, std::string_view kind,
                  std::string_view where);
void requireNonEmpty(const DataValue &value, std::string_view kind,
                     std::string_view where);

// Whole numbers print as integers, anything else to two decimals. std::to_string
// printed 0.000000, which is noise in a message a human reads.
[[nodiscard]] std::string readable(float value);

} // namespace demi::assets::data_read
