#include "demi/packages/PackageHash.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <mbedtls/sha256.h>
#include <sstream>

namespace demi::packages {
namespace {

std::string encodeHash(const std::array<unsigned char, 32> &hash) {
  std::ostringstream output;
  output << "sha256:" << std::hex << std::setfill('0');
  for (const unsigned char byte : hash)
    output << std::setw(2) << static_cast<int>(byte);
  return output.str();
}

} // namespace

std::string sha256Text(const std::string_view text) {
  std::array<unsigned char, 32> hash{};
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(text.data()),
                 text.size(), hash.data(), 0);
  return encodeHash(hash);
}

std::optional<std::string> sha256File(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    return std::nullopt;
  }
  std::array<unsigned char, 64 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    if (input.gcount() > 0)
      mbedtls_sha256_update(&context, buffer.data(),
                            static_cast<std::size_t>(input.gcount()));
  }
  std::array<unsigned char, 32> hash{};
  const int result = mbedtls_sha256_finish(&context, hash.data());
  mbedtls_sha256_free(&context);
  return result == 0 ? std::make_optional(encodeHash(hash)) : std::nullopt;
}

} // namespace demi::packages
