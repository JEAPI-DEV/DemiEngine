#include "demi/assets/TerrainAssetPayload.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainAssetPayloadData.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace demi::assets::terrain_payload {
namespace {

constexpr std::array<char, 8> Magic{'D', 'M', 'T', 'R', 'A', 'S', 'T', '5'};
static_assert(terrainAssetPayloadVersion == 5,
              "Update terrain payload magic when changing its format");
static_assert(Magic.back() == static_cast<char>('0' + Version));
constexpr std::size_t HeaderSize = Magic.size() + 3 * sizeof(std::uint64_t);
constexpr std::size_t DigestSize = sizeof("fnv1a64:0000000000000000") - 1;

void appendLength(std::vector<std::byte> &output, std::size_t length) {
  if (length > std::numeric_limits<std::uint64_t>::max())
    throw std::length_error("Terrain payload section exceeds u64 length");
  const auto value = static_cast<std::uint64_t>(length);
  for (unsigned shift = 0; shift < 64; shift += 8)
    output.push_back(std::byte((value >> shift) & 0xffU));
}

std::size_t readLength(std::span<const std::byte> input, std::size_t offset) {
  if (offset > input.size() || input.size() - offset < sizeof(std::uint64_t))
    throw std::invalid_argument("Terrain payload has a truncated header");
  std::uint64_t value = 0;
  for (unsigned shift = 0; shift < 64; shift += 8)
    value |= std::uint64_t(std::to_integer<unsigned>(input[offset + shift / 8]))
             << shift;
  if (value > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("Terrain payload section exceeds host size");
  return static_cast<std::size_t>(value);
}

std::string digest(std::span<const std::byte> bytes) {
  return hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()));
}

std::vector<std::byte> cookedBytes(const runtime::HeightField &field,
                                   std::string_view recipeDigest,
                                   std::string_view inputFingerprint) {
  const runtime::TerrainCookInput input{
      .field = &field,
      .generatorVersionTag = std::to_string(runtime::terrainGeneratorVersion),
      .recipeDigest = std::string(recipeDigest),
      .inputFingerprint = std::string(inputFingerprint)};
  std::string error;
  auto cooked = runtime::cookTerrainField(input, &error);
  if (!cooked)
    throw std::invalid_argument("Cannot prepare terrain payload: " + error);
  auto bytes = runtime::serializeTerrainCookedField(*cooked);
  if (bytes.empty())
    throw std::invalid_argument("Cannot encode cooked terrain field");
  return bytes;
}

runtime::TerrainCookedField readCooked(std::span<const std::byte> bytes) {
  std::string error;
  auto cooked = runtime::deserializeTerrainCookedField(bytes, &error);
  if (!cooked)
    throw std::invalid_argument("Invalid cooked terrain section: " + error);
  return std::move(*cooked);
}

void verifyBase(const runtime::HeightField &field,
                const runtime::HeightField &base) {
  if (base.cellsX != field.cellsX || base.cellsZ != field.cellsZ ||
      base.size.x != field.size.x || base.size.y != field.size.y ||
      base.biomeIds != field.biomeIds ||
      base.heights.size() != field.heights.size())
    throw std::invalid_argument(
        "Terrain graph checkpoint disagrees with its final grid");
}

} // namespace

std::vector<std::byte> encode(const runtime::HeightField &field,
                              std::string_view recipeDigest,
                              std::string_view inputFingerprint) {
  const auto main = cookedBytes(field, recipeDigest, inputFingerprint);
  std::vector<std::byte> base;
  if (field.graphArtifacts) {
    if (!field.graphArtifacts->baseField)
      throw std::invalid_argument(
          "Cannot prepare graph terrain without its unedited checkpoint");
    verifyBase(field, *field.graphArtifacts->baseField);
    auto checkpoint = *field.graphArtifacts->baseField;
    // A pre-edit checkpoint has no exclusion layer yet. Its omitted samples
    // represent zero, unlike a partially populated final field.
    if (checkpoint.exclusions.empty())
      checkpoint.exclusions.resize(checkpoint.heights.size());
    base = cookedBytes(checkpoint, recipeDigest, inputFingerprint);
  }
  const auto derived = encodeDerived(field);
  // JSON writes non-finite numbers as null. Check the structured data through
  // its own reader before it can replace a known-good prepared cache.
  auto checked = field;
  decodeDerived(derived, checked);
  const std::string metadata = derived.dump();
  const auto maximum = std::numeric_limits<std::size_t>::max();
  if (main.size() > maximum - HeaderSize - DigestSize ||
      base.size() > maximum - HeaderSize - DigestSize - main.size() ||
      metadata.size() >
          maximum - HeaderSize - DigestSize - main.size() - base.size())
    throw std::length_error("Terrain payload is too large for this host");

  std::vector<std::byte> output;
  output.reserve(HeaderSize + main.size() + base.size() + metadata.size() +
                 DigestSize);
  for (char character : Magic)
    output.push_back(std::byte(static_cast<unsigned char>(character)));
  appendLength(output, main.size());
  appendLength(output, base.size());
  appendLength(output, metadata.size());
  output.insert(output.end(), main.begin(), main.end());
  output.insert(output.end(), base.begin(), base.end());
  for (char character : metadata)
    output.push_back(std::byte(static_cast<unsigned char>(character)));
  const std::string checksum = digest(output);
  for (char character : checksum)
    output.push_back(std::byte(static_cast<unsigned char>(character)));
  return output;
}

std::shared_ptr<const runtime::HeightField>
decode(std::span<const std::byte> bytes, std::string *recipeDigest,
       std::string *inputFingerprint) {
  if (bytes.size() < HeaderSize + DigestSize)
    throw std::invalid_argument("Terrain payload header is truncated");
  for (std::size_t index = 0; index < Magic.size(); ++index)
    if (bytes[index] != std::byte(static_cast<unsigned char>(Magic[index])))
      throw std::invalid_argument("Terrain payload has an unknown format");
  const auto mainLength = readLength(bytes, Magic.size());
  const auto baseLength = readLength(bytes, Magic.size() + 8);
  const auto metadataLength = readLength(bytes, Magic.size() + 16);
  std::size_t remaining = bytes.size() - HeaderSize - DigestSize;
  if (mainLength == 0 || mainLength > remaining)
    throw std::invalid_argument("Terrain payload main field is truncated");
  remaining -= mainLength;
  if (baseLength > remaining)
    throw std::invalid_argument(
        "Terrain payload graph checkpoint is truncated");
  remaining -= baseLength;
  if (metadataLength != remaining)
    throw std::invalid_argument("Terrain payload metadata length is invalid");
  const auto data = bytes.first(bytes.size() - DigestSize);
  const auto checksum = bytes.last(DigestSize);
  const std::string expected = digest(data);
  if (!std::equal(expected.begin(), expected.end(), checksum.begin(),
                  [](char left, std::byte right) {
                    return static_cast<unsigned char>(left) ==
                           std::to_integer<unsigned char>(right);
                  }))
    throw std::invalid_argument("Terrain payload checksum does not match");

  const auto main = readCooked(bytes.subspan(HeaderSize, mainLength));
  auto loaded = std::make_shared<runtime::HeightField>(
      *runtime::loadTerrainCookedField(main));
  if (recipeDigest)
    *recipeDigest = main.recipeDigest;
  if (inputFingerprint)
    *inputFingerprint = main.inputFingerprint;
  const auto metadataOffset = HeaderSize + mainLength + baseLength;
  const std::string metadata(
      reinterpret_cast<const char *>(bytes.data() + metadataOffset),
      metadataLength);
  const auto derived = nlohmann::json::parse(metadata);
  decodeDerived(derived, *loaded);

  if (baseLength != 0) {
    if (!loaded->graphArtifacts)
      throw std::invalid_argument(
          "Terrain payload includes a graph checkpoint without graph metadata");
    const auto base =
        readCooked(bytes.subspan(HeaderSize + mainLength, baseLength));
    if (base.recipeDigest != main.recipeDigest ||
        base.inputFingerprint != main.inputFingerprint)
      throw std::invalid_argument(
          "Terrain graph checkpoint provenance differs from final field");
    const auto checkpoint = runtime::loadTerrainCookedField(base);
    verifyBase(*loaded, *checkpoint);
    auto artifacts = std::make_shared<runtime::TerrainGraphArtifacts>(
        *loaded->graphArtifacts);
    artifacts->baseField = checkpoint;
    loaded->graphArtifacts = std::move(artifacts);
  } else if (loaded->graphArtifacts) {
    throw std::invalid_argument("Terrain payload lacks its graph checkpoint");
  }
  return loaded;
}

} // namespace demi::assets::terrain_payload
