#include "demi/assets/AssetHash.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace demi::assets {
namespace {

constexpr std::uint64_t Offset = 14695981039346656037ULL;
constexpr std::uint64_t Prime = 1099511628211ULL;

void append(std::uint64_t &hash, const unsigned char value) {
  hash ^= value;
  hash *= Prime;
}

std::string encode(const std::uint64_t hash) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16)
         << hash;
  return output.str();
}

bool appendFile(std::uint64_t &hash, const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    for (std::streamsize index = 0; index < input.gcount(); ++index)
      append(hash, static_cast<unsigned char>(buffer[index]));
  }
  return true;
}

} // namespace

std::string hashBytes(const std::span<const unsigned char> bytes) {
  std::uint64_t hash = Offset;
  for (const unsigned char byte : bytes)
    append(hash, byte);
  return encode(hash);
}

std::optional<std::string> hashFile(const std::filesystem::path &path) {
  std::uint64_t hash = Offset;
  return appendFile(hash, path) ? std::make_optional(encode(hash))
                                : std::nullopt;
}

std::optional<std::string>
hashFiles(const std::vector<std::filesystem::path> &paths) {
  std::vector<std::filesystem::path> sorted = paths;
  std::ranges::sort(sorted);
  std::uint64_t hash = Offset;
  for (const auto &path : sorted) {
    const std::string label = path.filename().generic_string();
    for (const unsigned char byte : label)
      append(hash, byte);
    append(hash, 0);
    if (!appendFile(hash, path))
      return std::nullopt;
  }
  return encode(hash);
}

} // namespace demi::assets
