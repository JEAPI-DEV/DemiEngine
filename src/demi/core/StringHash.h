#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace demi {

// Hash owning strings and borrowed views identically without constructing a
// key.
struct StringHash {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

} // namespace demi
