#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace demi::runtime {

struct Vec2;
struct Vec3;
struct Color;

// Keep JSON implementation out of lightweight component headers. These codecs
// only cover native values whose authored representation is unambiguous.
bool runtimeFieldJson(bool value, nlohmann::json &out);
bool runtimeFieldJson(std::int64_t value, nlohmann::json &out);
bool runtimeFieldJson(std::uint64_t value, nlohmann::json &out);
bool runtimeFieldJson(float value, nlohmann::json &out);
bool runtimeFieldJson(double value, nlohmann::json &out);
bool runtimeFieldJson(const std::string &value, nlohmann::json &out);
bool runtimeFieldJson(const Vec2 &value, nlohmann::json &out);
bool runtimeFieldJson(const Vec3 &value, nlohmann::json &out);
bool runtimeFieldJson(const Color &value, nlohmann::json &out);
bool runtimeFieldJson(const std::vector<Vec2> &value, nlohmann::json &out);
bool runtimeFieldJson(const std::vector<Vec3> &value, nlohmann::json &out);

template <typename Value>
bool runtimeFieldJson(const Value &value, nlohmann::json &out) {
  if constexpr (std::is_floating_point_v<Value>) {
    return runtimeFieldJson(static_cast<double>(value), out);
  } else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) {
    return runtimeFieldJson(static_cast<std::int64_t>(value), out);
  } else if constexpr (std::is_integral_v<Value>) {
    return runtimeFieldJson(static_cast<std::uint64_t>(value), out);
  } else {
    // Enums and compound properties require component codecs.
    return false;
  }
}

// Copies one validated authored property without replacing simulation-owned
// fields on the destination component. Parsing remains the component's job.
template <typename ComponentType> struct RuntimeFieldBinding {
  std::string_view name;
  void (*copy)(ComponentType &, const ComponentType &);
  bool (*read)(const ComponentType &, nlohmann::json &) = nullptr;
  bool advertiseDefault = true;

  // Presence-sensitive authoring sugar may have no equivalent explicit value.
  [[nodiscard]] constexpr RuntimeFieldBinding withoutDefault() const {
    auto result = *this;
    result.advertiseDefault = false;
    return result;
  }

  template <auto Member>
  static constexpr RuntimeFieldBinding member(std::string_view name) {
    return {name,
            [](ComponentType &destination, const ComponentType &source) {
              destination.*Member = source.*Member;
            },
            [](const ComponentType &source, nlohmann::json &out) {
              return runtimeFieldJson(source.*Member, out);
            }};
  }

  template <auto... Members>
  static constexpr RuntimeFieldBinding members(std::string_view name) {
    static_assert(sizeof...(Members) > 0,
                  "A compound field must update a member");
    return {name, [](ComponentType &destination, const ComponentType &source) {
              ((destination.*Members = source.*Members), ...);
            }};
  }

  // Parser-owned dependencies travel with the edited property, but only the
  // primary member defines its authored value/default.
  template <auto Primary, auto... Derived>
  static constexpr RuntimeFieldBinding memberWithDerived(std::string_view name) {
    auto binding = member<Primary>(name);
    binding.copy = [](ComponentType &destination, const ComponentType &source) {
      destination.*Primary = source.*Primary;
      ((destination.*Derived = source.*Derived), ...);
    };
    return binding;
  }

  template <auto Parent, auto Member>
  static constexpr RuntimeFieldBinding nestedMember(std::string_view name) {
    return {name,
            [](ComponentType &destination, const ComponentType &source) {
              (destination.*Parent).*Member = (source.*Parent).*Member;
            },
            [](const ComponentType &source, nlohmann::json &out) {
              return runtimeFieldJson((source.*Parent).*Member, out);
            }};
  }
};

} // namespace demi::runtime
