#include "demi/runtime/terrain/TerrainCookFormat.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace demi::runtime::cook_format {
namespace {


// A cooked heightfield is read back by a different process, a different
// toolchain and a different engine build than the one that wrote it, and it is
// read from a directory a build tool produced rather than from a
// version-controlled source file. So nothing about the file's shape is trusted:
// the magic and byte-order marker say how to read it, the version says whether
// the fields below mean anything, every count is checked against the bytes that
// actually remain, and a probe float says whether this build can represent what
// was stored.
constexpr std::array<std::uint8_t, 8> kMagic{'D', 'M', 'T', 'R', 'C', 'O', 'K',
                                            '1'};
// Written and read as a little-endian u32. A big-endian reader assembling the
// same four bytes gets 0x04030201 and refuses the file rather than interpreting
// every field after it backwards.
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::uint32_t kHeaderBytes = 64;
constexpr std::uint32_t kFlagMasksPresent = 1U;
constexpr std::uint32_t kKnownFlags = kFlagMasksPresent;
// 1.0F is exact in every binary32 format, so reading it back and failing means
// this build's float width or representation differs from the writer's.
constexpr float kFloatProbe = 1.0F;

static_assert(sizeof(float) == 4, "The cooked layout stores binary32 floats");
static_assert(std::numeric_limits<float>::is_iec559,
              "The cooked layout stores IEEE-754 binary32 floats");

// FNV-1a 64, the same primitive demi::assets::hashBytes() uses, so a cooked
// terrain's digest and a cooked asset's digest are the same kind of statement
// about content. It is repeated here rather than reached for because this file
// needs the raw 64-bit value and the shared helper returns formatted text. The
// two must be changed together.
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv1a64(const std::byte *bytes, std::size_t count) {
  std::uint64_t hash = kFnvOffset;
  for (std::size_t index = 0; index < count; ++index) {
    hash ^= std::to_integer<std::uint8_t>(bytes[index]);
    hash *= kFnvPrime;
  }
  return hash;
}

// Every scalar crosses the boundary as explicit little-endian bytes rather than
// by copying the host representation, so the file's byte order is the writer's
// choice and the endian marker describes it accurately on any host.
template <class T> void appendLittle(std::vector<std::byte> &out, T value) {
  static_assert(std::is_trivially_copyable_v<T>);
  std::array<unsigned char, sizeof(T)> raw{};
  std::memcpy(raw.data(), &value, sizeof(T));
  if constexpr (std::endian::native == std::endian::big)
    std::reverse(raw.begin(), raw.end());
  for (const auto byte : raw)
    out.push_back(static_cast<std::byte>(byte));
}

template <class T> bool readLittle(std::span<const std::byte> bytes,
                                   std::size_t offset, T &out) {
  if (offset + sizeof(T) > bytes.size())
    return false;
  std::array<unsigned char, sizeof(T)> raw{};
  for (std::size_t index = 0; index < sizeof(T); ++index)
    raw[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
  if constexpr (std::endian::native == std::endian::big)
    std::reverse(raw.begin(), raw.end());
  std::memcpy(&out, raw.data(), sizeof(T));
  return true;
}

void setErrorImpl(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
}

// A cursor that can only hand out bytes that exist. Every read reports failure
// instead of running off the end, so a truncated or hostile file produces a
// message rather than an out-of-bounds access.
class Reader {
public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }
  [[nodiscard]] std::size_t offset() const { return offset_; }
  [[nodiscard]] bool exhausted() const { return offset_ == bytes_.size(); }

  template <class T> bool scalar(T &out) {
    if (!readLittle(bytes_, offset_, out))
      return false;
    offset_ += sizeof(T);
    return true;
  }

  bool text(std::string &out, std::size_t size) {
    if (size > remaining())
      return false;
    out.assign(size, '\0');
    for (std::size_t index = 0; index < size; ++index)
      out[index] = static_cast<char>(std::to_integer<std::uint8_t>(
          bytes_[offset_ + index]));
    offset_ += size;
    return true;
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

// A count is only trusted once it fits in the bytes that remain. This is the
// check that stops a corrupt file from asking for a multi-gigabyte allocation
// before anything has been reserved.
bool readCount(Reader &reader, std::string_view name, std::size_t elementBytes,
               std::size_t &out, std::string &error) {
  std::int32_t count = 0;
  if (!reader.scalar(count)) {
    error = "Terrain cook payload ended inside the '" + std::string(name) +
            "' count.";
    return false;
  }
  if (count < 0) {
    error = "Terrain cook '" + std::string(name) +
            "' count is negative; the file is corrupt.";
    return false;
  }
  const auto count64 = static_cast<std::size_t>(count);
  if (elementBytes != 0 && count64 > reader.remaining() / elementBytes) {
    error = "Terrain cook '" + std::string(name) + "' count of " +
            std::to_string(count64) +
            " exceeds the bytes left in the payload; the file is truncated or "
            "corrupt.";
    return false;
  }
  out = count64;
  return true;
}

bool readFloatArray(Reader &reader, std::string_view name,
                    std::size_t expected, std::vector<float> &out,
                    std::string &error) {
  std::size_t count = 0;
  if (!readCount(reader, name, sizeof(float), count, error))
    return false;
  if (count != expected) {
    error = "Terrain cook '" + std::string(name) + "' count of " +
            std::to_string(count) + " does not match the grid's " +
            std::to_string(expected) + " samples.";
    return false;
  }
  out.assign(count, 0.0F);
  for (std::size_t index = 0; index < count; ++index) {
    if (!reader.scalar(out[index])) {
      error = "Terrain cook payload ended inside '" + std::string(name) + "'.";
      return false;
    }
    // A NaN or infinity here would propagate into the terrain mesh, the
    // collision shapes and the derived masks, where it silently poisons every
    // later comparison instead of failing where it entered.
    if (!std::isfinite(out[index])) {
      error = "Terrain cook " + std::string(name) + " sample " +
              std::to_string(index) + " is not finite.";
      return false;
    }
  }
  return true;
}

bool readIndexArray(Reader &reader, std::string_view name,
                    std::size_t expected, std::vector<std::uint32_t> &out,
                    std::string &error) {
  std::size_t count = 0;
  if (!readCount(reader, name, sizeof(std::uint32_t), count, error))
    return false;
  if (count != expected) {
    error = "Terrain cook '" + std::string(name) + "' count of " +
            std::to_string(count) + " does not match the grid's " +
            std::to_string(expected) + " samples.";
    return false;
  }
  out.assign(count, 0);
  for (auto &value : out) {
    if (!reader.scalar(value)) {
      error = "Terrain cook payload ended inside '" + std::string(name) + "'.";
      return false;
    }
  }
  return true;
}

bool readText(Reader &reader, std::string_view name, std::string &out,
              std::string &error) {
  std::size_t count = 0;
  if (!readCount(reader, name, 1, count, error))
    return false;
  if (!reader.text(out, count)) {
    error = "Terrain cook payload ended inside the '" + std::string(name) +
            "' text.";
    return false;
  }
  return true;
}

// (cellsX + 1) * (cellsZ + 1) samples, matching HeightField::index(). Both the
// multiplication and the size are checked, because a grid read from a file can
// claim any dimensions at all.
bool gridSampleCountImpl(int cellsX, int cellsZ, std::size_t &out,
                     std::string &error) {
  if (cellsX < 0 || cellsZ < 0) {
    error = "Terrain grid dimensions must not be negative.";
    return false;
  }
  const auto width = static_cast<std::uint64_t>(cellsX) + 1U;
  const auto depth = static_cast<std::uint64_t>(cellsZ) + 1U;
  const auto total = width * depth;
  if (total > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
      total > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
    error = "Terrain grid does not fit in memory on this platform.";
    return false;
  }
  out = static_cast<std::size_t>(total);
  return true;
}

// A chunk list is a claim that the field is tiled, not merely a list. Overlapping
// or missing cells would let a consumer resolve a sample to two chunks or none,
// which is exactly the ambiguity a cached file must not reintroduce.
bool validateChunks(const std::vector<TerrainChunk> &chunks, int cellsX,
                    int cellsZ, std::string &error) {
  if (chunks.empty()) {
    if (cellsX == 0 || cellsZ == 0)
      return true;
    error = "Terrain chunk list is empty for a non-empty grid.";
    return false;
  }
  if (chunks.size() > std::size_t(cellsX) * std::size_t(cellsZ)) {
    error = "Terrain has more chunks than cells.";
    return false;
  }
  std::vector<unsigned char> covered(std::size_t(cellsX) * std::size_t(cellsZ),
                                    0);
  for (const auto &chunk : chunks) {
    if (chunk.cellsX <= 0 || chunk.cellsZ <= 0 || chunk.firstCellX < 0 ||
        chunk.firstCellZ < 0 || chunk.firstCellX > cellsX - chunk.cellsX ||
        chunk.firstCellZ > cellsZ - chunk.cellsZ) {
      error = "Terrain chunk rectangle falls outside the grid.";
      return false;
    }
    for (int z = chunk.firstCellZ; z < chunk.firstCellZ + chunk.cellsZ; ++z) {
      for (int x = chunk.firstCellX; x < chunk.firstCellX + chunk.cellsX; ++x) {
        auto &cell = covered[std::size_t(z) * std::size_t(cellsX) +
                             std::size_t(x)];
        if (cell != 0) {
          error = "Terrain chunk rectangles overlap.";
          return false;
        }
        cell = 1;
      }
    }
  }
  if (std::ranges::find(covered, static_cast<unsigned char>(0)) !=
      covered.end()) {
    error = "Terrain chunk rectangles do not cover the whole grid.";
    return false;
  }
  return true;
}

// The shape rules a cooked field must satisfy. These are the same ones the
// in-memory generation cache enforces before publishing a field, so a payload
// that loads can always be published and a field that cooks can always be
// stored; the two paths may not disagree about what a usable field is.
bool validateField(const TerrainCookedField &field, std::size_t &samples,
                   std::string &error) {
  if (!gridSampleCountImpl(field.cellsX, field.cellsZ, samples, error))
    return false;
  if (!std::isfinite(field.size.x) || !std::isfinite(field.size.y) ||
      field.size.x <= 0 || field.size.y <= 0) {
    error = "Terrain world size must be finite and positive.";
    return false;
  }
  if (!std::isfinite(field.seaLevel)) {
    error = "Terrain water level must be finite.";
    return false;
  }
  if (field.recipeDigest.empty()) {
    error = "Terrain cook recipe digest is missing.";
    return false;
  }
  if (field.biomeIds.size() != field.biomeColors.size()) {
    error = "Terrain biome ids and colours disagree in count.";
    return false;
  }
  if (field.biomeMaterials.size() != field.biomeIds.size() ||
      field.biomeTextureScales.size() != field.biomeIds.size()) {
    error = "Terrain appearance arrays do not match its biome palette.";
    return false;
  }
  for (std::size_t index = 0; index < field.biomeIds.size(); ++index) {
    const auto &material = field.biomeMaterials[index];
    if ((!material.empty() &&
         (!material.starts_with("asset://") || material.size() <= 8)) ||
        !std::isfinite(field.biomeTextureScales[index]) ||
        field.biomeTextureScales[index] <= 0) {
      error = "Terrain biome appearance requires an asset URI and positive finite texture scale.";
      return false;
    }
  }
  if (field.baseHeights.size() != samples || field.heights.size() != samples ||
      field.normalsX.size() != samples || field.normalsY.size() != samples ||
      field.normalsZ.size() != samples ||
      field.biomeIndices.size() != samples ||
      field.exclusions.size() != samples) {
    error = "Terrain sample arrays do not match the grid's sample count.";
    return false;
  }
  if (field.masksPresent &&
      (field.slope.size() != samples || field.moisture.size() != samples ||
       field.waterDistance.size() != samples || field.flow.size() != samples ||
       field.sediment.size() != samples ||
       field.substrates.size() != samples)) {
    error = "Terrain mask arrays do not match the grid's sample count.";
    return false;
  }
  if (!field.masksPresent &&
      (!field.slope.empty() || !field.moisture.empty() ||
       !field.waterDistance.empty() || !field.flow.empty() ||
       !field.sediment.empty() || !field.substrates.empty())) {
    error = "Terrain payload carries mask samples it does not claim to have.";
    return false;
  }
  for (std::size_t sample = 0; sample < samples; ++sample) {
    if (!std::isfinite(field.baseHeights[sample]) ||
        !std::isfinite(field.heights[sample])) {
      error = "Terrain height sample " + std::to_string(sample) +
              " is not finite.";
      return false;
    }
    if (!std::isfinite(field.exclusions[sample]) ||
        field.exclusions[sample] < 0 || field.exclusions[sample] > 1) {
      error = "Terrain exclusion sample " + std::to_string(sample) +
              " is outside [0, 1].";
      return false;
    }
    const auto nx = field.normalsX[sample];
    const auto ny = field.normalsY[sample];
    const auto nz = field.normalsZ[sample];
    if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
      error = "Terrain normal sample " + std::to_string(sample) +
              " is not finite.";
      return false;
    }
    const auto length = std::hypot(double(nx), double(ny), double(nz));
    if (ny < 0 || std::abs(length - 1) > 1e-4) {
      error = "Terrain normal sample " + std::to_string(sample) +
              " is not an upward unit vector.";
      return false;
    }
    // A field with no biomes is a legitimate "nothing was assigned" state and
    // has exactly one representable index. Anything else would be a claim about
    // a biome list the file does not carry.
    if (field.biomeIds.empty() ? field.biomeIndices[sample] != 0
                               : field.biomeIndices[sample] >=
                                     field.biomeIds.size()) {
      error = "Terrain biome index at sample " + std::to_string(sample) +
              " falls outside the biome list.";
      return false;
    }
  }
  for (std::size_t sample = 0; sample < field.slope.size(); ++sample) {
    if (!std::isfinite(field.slope[sample]) ||
        !std::isfinite(field.moisture[sample]) ||
        !std::isfinite(field.waterDistance[sample]) ||
        !std::isfinite(field.flow[sample]) ||
        !std::isfinite(field.sediment[sample])) {
      error = "Terrain mask sample " + std::to_string(sample) + " is not finite.";
      return false;
    }
  }
  for (const auto &colour : field.biomeColors) {
    if (!std::isfinite(colour.r) || !std::isfinite(colour.g) ||
        !std::isfinite(colour.b) || !std::isfinite(colour.a)) {
      error = "Terrain biome colour is not finite.";
      return false;
    }
  }
  return validateChunks(field.chunks, field.cellsX, field.cellsZ, error);
}

// TerrainSamples is paged copy-on-write storage with no contiguous span
// accessor, so packing iterates through operator[] and the loader rebuilds
// through resize()/set(). Reported rather than worked around by editing a file
// another owner may be changing; a span() overload would make both of these
// straight copies without changing what they mean.
template <class Samples> std::vector<float> flattenImpl(const Samples &samples) {
  std::vector<float> values;
  values.reserve(samples.size());
  for (std::size_t index = 0; index < samples.size(); ++index)
    values.push_back(samples[index]);
  return values;
}

template <class Samples>
std::vector<std::uint32_t> flattenIndicesImpl(const Samples &samples) {
  std::vector<std::uint32_t> values;
  values.reserve(samples.size());
  for (std::size_t index = 0; index < samples.size(); ++index)
    values.push_back(static_cast<std::uint32_t>(samples[index]));
  return values;
}

void appendFloats(std::vector<std::byte> &out, const std::vector<float> &values) {
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(values.size()));
  for (const auto value : values)
    appendLittle<float>(out, value);
}

void appendIndices(std::vector<std::byte> &out,
                   const std::vector<std::uint32_t> &values) {
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(values.size()));
  for (const auto value : values)
    appendLittle<std::uint32_t>(out, value);
}

void appendText(std::vector<std::byte> &out, std::string_view text) {
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(text.size()));
  for (const auto character : text)
    out.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
}

// Every byte of the payload except the eight-byte digest trailer.
//
// The 64-byte fixed header:
//   0  u8[8]  magic "DMTRCOK1"
//   8  u32    little-endian marker 0x01020304
//  12  i32    payload version
//  16  u32    header size, 64
//  20  u32    flags; bit 0 = derived masks present
//  24  f32    float probe, 1.0
//  28  f32    world size x
//  32  f32    world size y
//  36  f32    sea level
//  40  i32    cells X
//  44  i32    cells Z
//  48  u32    reserved, zero
//  52  u32    reserved, zero
//  56  u64    sample count, redundant with the grid on purpose
// Then per-block: i32 count, then count elements. Floats are IEEE-754 binary32
// little-endian, indices and counts are signed 32-bit so a corrupted count reads
// as negative rather than as a huge allocation request, and strings are an i32
// byte length followed by UTF-8 bytes. The fixed height/normal/biome/exclusion
// blocks come first in the order the struct declares them, then the mask blocks
// (present only when the flag is set), then the biome, chunk and provenance
// tables.
std::vector<std::byte> serializeBody(const TerrainCookedField &field) {
  std::vector<std::byte> out;
  out.reserve(kHeaderBytes);
  for (const auto byte : kMagic)
    out.push_back(static_cast<std::byte>(byte));
  appendLittle<std::uint32_t>(out, kEndianMarker);
  appendLittle<std::int32_t>(out, field.version);
  appendLittle<std::uint32_t>(out, kHeaderBytes);
  appendLittle<std::uint32_t>(out, field.masksPresent ? kFlagMasksPresent : 0U);
  appendLittle<float>(out, kFloatProbe);
  appendLittle<float>(out, field.size.x);
  appendLittle<float>(out, field.size.y);
  appendLittle<float>(out, field.seaLevel);
  appendLittle<std::int32_t>(out, field.cellsX);
  appendLittle<std::int32_t>(out, field.cellsZ);
  appendLittle<std::uint32_t>(out, 0);
  appendLittle<std::uint32_t>(out, 0);
  std::size_t samples = 0;
  std::string ignored;
  (void)gridSampleCountImpl(field.cellsX, field.cellsZ, samples, ignored);
  appendLittle<std::uint64_t>(out, static_cast<std::uint64_t>(samples));

  appendFloats(out, field.heights);
  appendFloats(out, field.baseHeights);
  appendFloats(out, field.normalsX);
  appendFloats(out, field.normalsY);
  appendFloats(out, field.normalsZ);
  appendIndices(out, field.biomeIndices);
  appendFloats(out, field.exclusions);
  if (field.masksPresent) {
    appendFloats(out, field.slope);
    appendFloats(out, field.moisture);
    appendFloats(out, field.waterDistance);
    appendFloats(out, field.flow);
    appendFloats(out, field.sediment);
    appendIndices(out, field.substrates);
  }
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(field.biomeIds.size()));
  for (const auto &id : field.biomeIds)
    appendText(out, id);
  appendLittle<std::int32_t>(out,
                            static_cast<std::int32_t>(field.biomeColors.size()));
  for (const auto &colour : field.biomeColors) {
    appendLittle<float>(out, colour.r);
    appendLittle<float>(out, colour.g);
    appendLittle<float>(out, colour.b);
    appendLittle<float>(out, colour.a);
  }
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(field.biomeMaterials.size()));
  for (const auto &material : field.biomeMaterials)
    appendText(out, material);
  appendFloats(out, field.biomeTextureScales);
  appendLittle<std::int32_t>(out, static_cast<std::int32_t>(field.chunks.size()));
  for (const auto &chunk : field.chunks) {
    appendLittle<std::int32_t>(out, chunk.firstCellX);
    appendLittle<std::int32_t>(out, chunk.firstCellZ);
    appendLittle<std::int32_t>(out, chunk.cellsX);
    appendLittle<std::int32_t>(out, chunk.cellsZ);
  }
  appendText(out, field.generatorVersionTag);
  appendText(out, field.recipeDigest);
  appendText(out, field.inputFingerprint);
  appendText(out, field.stageOrder);
  appendText(out, field.quality);
  appendText(out, field.paletteId);
  return out;
}

// The digest of a payload: FNV-1a over every byte that precedes the trailer, so
// one flipped byte anywhere in the file is detected.
std::uint64_t payloadDigest(const std::vector<std::byte> &body) {
  return fnv1a64(body.data(), body.size());
}

// Color is a plain aggregate with no equality operator, so the comparison a
// staleness check needs is written once here rather than inlined per field.
bool sameColors(const std::vector<Color> &left, const std::vector<Color> &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].r != right[index].r || left[index].g != right[index].g ||
        left[index].b != right[index].b || left[index].a != right[index].a)
      return false;
  }
  return true;
}

// The field-only projection of a payload: provenance, grid and samples, with the
// derived masks dropped. A HeightField cannot carry masks, so this is what a
// reloaded field is compared against, and the comparison stays independent of
// whether the original cook had a mask set.
TerrainCookedField fieldProjection(const TerrainCookedField &field) {
  TerrainCookedField projection = field;
  projection.slope.clear();
  projection.moisture.clear();
  projection.waterDistance.clear();
  projection.flow.clear();
  projection.sediment.clear();
  projection.substrates.clear();
  projection.masksPresent = false;
  return projection;
}


} // namespace

// The recipe-side projection from paged samples to the flat arrays the format
// stores. Shared because the cook must produce exactly the arrays the writer
// would have produced, or a re-cook of a loaded field would not be byte-equal.
std::vector<float> flatten(const TerrainSamples<float> &samples) {
  return flattenImpl(samples);
}
std::vector<std::uint32_t>
flattenIndices(const TerrainSamples<std::size_t> &samples) {
  return flattenIndicesImpl(samples);
}

void setError(std::string *error, std::string message) {
  setErrorImpl(error, std::move(message));
}

bool gridSampleCount(const int cellsX, const int cellsZ, std::size_t &out,
                     std::string &error) {
  return gridSampleCountImpl(cellsX, cellsZ, out, error);
}

std::vector<std::byte> encode(const TerrainCookedField &field,
                              std::string &error) {
  std::size_t samples = 0;
  if (!validateField(field, samples, error))
    return {};
  auto bytes = serializeBody(field);
  appendLittle<std::uint64_t>(bytes, payloadDigest(bytes));
  return bytes;
}

bool decode(std::span<const std::byte> bytes, TerrainCookedField &field,
            std::string &error) {
  const auto fail = [&](std::string message) {
    error = std::move(message);
    return false;
  };

  if (bytes.size() < kHeaderBytes + sizeof(std::uint64_t))
    return fail("Terrain cook payload is shorter than its own header.");
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    if (std::to_integer<std::uint8_t>(bytes[index]) != kMagic[index])
      return fail("Terrain cook payload has the wrong magic; this file was not "
                  "written by the terrain cooker.");
  }
  std::uint32_t endianMarker = 0;
  (void)readLittle(bytes, 8, endianMarker);
  if (endianMarker != kEndianMarker)
    return fail("Terrain cook payload was not written little-endian.");
  std::int32_t version = 0;
  (void)readLittle(bytes, 12, version);
  if (version != terrainCookVersion)
    return fail("Terrain cook payload version " + std::to_string(version) +
                " does not match this build's version " +
                std::to_string(terrainCookVersion) + "; it is stale, not "
                "readable.");
  std::uint32_t headerBytes = 0;
  (void)readLittle(bytes, 16, headerBytes);
  if (headerBytes != kHeaderBytes)
    return fail("Terrain cook payload header size does not match this build.");
  std::uint32_t flags = 0;
  (void)readLittle(bytes, 20, flags);
  if ((flags & ~kKnownFlags) != 0)
    return fail("Terrain cook payload sets flags this build does not know.");
  float probe = 0;
  (void)readLittle(bytes, 24, probe);
  if (probe != kFloatProbe)
    return fail("Terrain cook payload float probe did not round-trip; this "
                "build cannot represent its floats.");

  field.version = version;
  (void)readLittle(bytes, 28, field.size.x);
  (void)readLittle(bytes, 32, field.size.y);
  (void)readLittle(bytes, 36, field.seaLevel);
  (void)readLittle(bytes, 40, field.cellsX);
  (void)readLittle(bytes, 44, field.cellsZ);
  std::uint32_t reserved0 = 0;
  std::uint32_t reserved1 = 0;
  std::uint64_t declaredSamples = 0;
  (void)readLittle(bytes, 48, reserved0);
  (void)readLittle(bytes, 52, reserved1);
  (void)readLittle(bytes, 56, declaredSamples);
  if (reserved0 != 0 || reserved1 != 0)
    return fail("Terrain cook payload reserved header bytes are not zero.");
  field.masksPresent = (flags & kFlagMasksPresent) != 0;

  std::string reason;
  std::size_t samples = 0;
  if (!gridSampleCountImpl(field.cellsX, field.cellsZ, samples, reason))
    return fail(std::move(reason));
  if (declaredSamples != samples)
    return fail("Terrain cook payload header disagrees with its own grid.");

  // The digest covers every byte before the trailer. It is checked LAST, after
  // the structure has been walked, because the structure gives the better
  // message for a truncated file and a wrong-endian or wrong-magic file is
  // already refused above.
  const auto body = bytes.subspan(0, bytes.size() - sizeof(std::uint64_t));
  std::uint64_t storedDigest = 0;
  (void)readLittle(bytes, bytes.size() - sizeof(std::uint64_t), storedDigest);

  Reader reader(body);
  std::uint32_t skipped = 0;
  for (std::size_t word = 0; word < kHeaderBytes / sizeof(std::uint32_t); ++word) {
    if (!reader.scalar(skipped))
      return fail("Terrain cook payload ended inside its header.");
  }

  const auto readFloats = [&](std::string_view name, std::vector<float> &out) {
    return readFloatArray(reader, name, samples, out, reason);
  };
  if (!readFloats("heights", field.heights) ||
      !readFloats("base_heights", field.baseHeights) ||
      !readFloats("normals_x", field.normalsX) ||
      !readFloats("normals_y", field.normalsY) ||
      !readFloats("normals_z", field.normalsZ) ||
      !readIndexArray(reader, "biome_indices", samples, field.biomeIndices,
                      reason) ||
      !readFloats("exclusions", field.exclusions))
    return fail(std::move(reason));
  if (field.masksPresent &&
      (!readFloats("slope", field.slope) ||
       !readFloats("moisture", field.moisture) ||
       !readFloats("water_distance", field.waterDistance) ||
       !readFloats("flow", field.flow) || !readFloats("sediment", field.sediment) ||
       !readIndexArray(reader, "substrates", samples, field.substrates, reason)))
    return fail(std::move(reason));

  std::size_t biomeCount = 0;
  if (!readCount(reader, "biome_ids", sizeof(std::int32_t), biomeCount, reason))
    return fail(std::move(reason));
  field.biomeIds.assign(biomeCount, {});
  for (auto &id : field.biomeIds)
    if (!readText(reader, "biome_id", id, reason))
      return fail(std::move(reason));

  std::size_t colourCount = 0;
  if (!readCount(reader, "biome_colours", 4 * sizeof(float), colourCount,
                 reason))
    return fail(std::move(reason));
  field.biomeColors.assign(colourCount, {});
  for (auto &colour : field.biomeColors) {
    if (!reader.scalar(colour.r) || !reader.scalar(colour.g) ||
        !reader.scalar(colour.b) || !reader.scalar(colour.a))
      return fail("Terrain cook payload ended inside 'biome_colours'.");
    if (!std::isfinite(colour.r) || !std::isfinite(colour.g) ||
        !std::isfinite(colour.b) || !std::isfinite(colour.a))
      return fail("Terrain cook biome colour is not finite.");
  }

  std::size_t materialCount = 0;
  if (!readCount(reader, "biome_materials", sizeof(std::int32_t), materialCount, reason))
    return fail(std::move(reason));
  if (materialCount != biomeCount)
    return fail("Terrain biome material count differs from its palette.");
  field.biomeMaterials.assign(materialCount, {});
  for (auto &material : field.biomeMaterials)
    if (!readText(reader, "biome_material", material, reason))
      return fail(std::move(reason));
  std::size_t scaleCount = 0;
  if (!readCount(reader, "biome_texture_scales", sizeof(float), scaleCount, reason))
    return fail(std::move(reason));
  if (scaleCount != biomeCount)
    return fail("Terrain texture scale count differs from its palette.");
  field.biomeTextureScales.assign(scaleCount, 1.0F);
  for (auto &scale : field.biomeTextureScales)
    if (!reader.scalar(scale) || !std::isfinite(scale) || scale <= 0)
      return fail("Terrain texture scale must be positive and finite.");

  std::size_t chunkCount = 0;
  if (!readCount(reader, "chunks", 4 * sizeof(std::int32_t), chunkCount,
                 reason))
    return fail(std::move(reason));
  if (chunkCount > std::size_t(field.cellsX) * std::size_t(field.cellsZ))
    return fail("Terrain cook has more chunks than cells.");
  field.chunks.assign(chunkCount, {});
  for (auto &chunk : field.chunks) {
    if (!reader.scalar(chunk.firstCellX) || !reader.scalar(chunk.firstCellZ) ||
        !reader.scalar(chunk.cellsX) || !reader.scalar(chunk.cellsZ))
      return fail("Terrain cook payload ended inside 'chunks'.");
  }

  if (!readText(reader, "generator_version", field.generatorVersionTag, reason) ||
      !readText(reader, "recipe_digest", field.recipeDigest, reason) ||
      !readText(reader, "input_fingerprint", field.inputFingerprint, reason) ||
      !readText(reader, "stage_order", field.stageOrder, reason) ||
      !readText(reader, "quality", field.quality, reason) ||
      !readText(reader, "palette_id", field.paletteId, reason))
    return fail(std::move(reason));

  // Nothing may follow the payload but the digest trailer. Trailing bytes mean
  // this is not the file the writer produced, so the digest is not the thing
  // that decides it.
  if (reader.offset() != body.size())
    return fail("Terrain cook payload has " +
                std::to_string(body.size() - reader.offset()) +
                " unexpected trailing bytes.");
  const auto computed = payloadDigest(std::vector<std::byte>(body.begin(),
                                                            body.end()));
  if (computed != storedDigest)
    return fail("Terrain cook payload digest does not match its contents; the "
                "file is corrupt.");
  field.contentHash = computed;
  if (!validateField(field, samples, reason))
    return fail(std::move(reason));
  error.clear();
  return true;
}

// Color has no equality operator, so tints are compared per channel.
bool sameColorsImpl(const std::vector<Color> &left,
                    const std::vector<Color> &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (left[index].r != right[index].r || left[index].g != right[index].g ||
        left[index].b != right[index].b || left[index].a != right[index].a)
      return false;
  return true;
}

bool sameColors(const std::vector<Color> &left, const std::vector<Color> &right) {
  return sameColorsImpl(left, right);
}

// Recomputed from the payload rather than trusted from the struct, so a payload
// damaged after it was written is caught here instead of being served to a world
// that would then disagree with its own collider.
std::uint64_t contentDigest(const TerrainCookedField &field) {
  return payloadDigest(serializeBody(field));
}

bool digestMatches(const TerrainCookedField &field) {
  return payloadDigest(serializeBody(field)) == field.contentHash;
}

bool validate(const TerrainCookedField &field, std::string &error,
              std::size_t *samplesOut) {
  std::size_t samples = 0;
  const bool valid = validateField(field, samples, error);
  if (valid && samplesOut != nullptr)
    *samplesOut = samples;
  return valid;
}

} // namespace demi::runtime::cook_format
