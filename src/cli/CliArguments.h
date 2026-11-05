#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <utility>

namespace demi::cli {

[[nodiscard]] inline std::optional<std::pair<int, int>>
parseWindowSize(std::string_view text) {
  const auto separator = text.find('x');
  if (separator == std::string_view::npos)
    return std::nullopt;
  int width = 0, height = 0;
  const auto first =
      std::from_chars(text.data(), text.data() + separator, width);
  const auto second = std::from_chars(text.data() + separator + 1,
                                      text.data() + text.size(), height);
  if (first.ec != std::errc{} || second.ec != std::errc{} ||
      first.ptr != text.data() + separator ||
      second.ptr != text.data() + text.size() || width < 1 || width > 65535 ||
      height < 1 || height > 65535)
    return std::nullopt;
  return std::pair{width, height};
}

} // namespace demi::cli
