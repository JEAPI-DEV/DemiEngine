#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace demi::packages {

[[nodiscard]] std::optional<std::string>
sha256File(const std::filesystem::path &path);
[[nodiscard]] std::string sha256Text(std::string_view text);

} // namespace demi::packages
