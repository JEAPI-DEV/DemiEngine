#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace demi::assets {

[[nodiscard]] std::string hashBytes(std::span<const unsigned char> bytes);
[[nodiscard]] std::optional<std::string>
hashFile(const std::filesystem::path &path);
[[nodiscard]] std::optional<std::string>
hashFiles(const std::vector<std::filesystem::path> &paths);

} // namespace demi::assets
