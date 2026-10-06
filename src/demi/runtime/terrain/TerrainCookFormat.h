#pragma once

#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainSamples.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The on-disk byte format for a cooked heightfield.
//
// This lives apart from TerrainCook.cpp because it changes for a different
// reason. The layout, the endian primitives, the bounds-checked reader and the
// digest change only when the FORMAT version changes; the cook, load and
// staleness rules in TerrainCook.cpp change whenever the generation pipeline
// does. Keeping them together made a 900-line file where a one-line format
// tweak sat next to unrelated validation.
namespace demi::runtime::cook_format {

// Shared by the format and the orchestration, because both reject on the same
// terms: a null out-parameter simply means nobody asked for a message.
void setError(std::string *error, std::string message);

// A grid's sample count, computed in a width that cannot overflow before the
// result is used to size anything.
[[nodiscard]] bool gridSampleCount(int cellsX, int cellsZ, std::size_t &out,
                                   std::string &error);

// The paged sample storage the field uses, projected into the flat arrays the
// format stores. Exposed because the cook must produce exactly the arrays the
// writer would have produced, or a re-cook of a loaded field would not be
// byte-equal to the file it came from.
[[nodiscard]] std::vector<float> flatten(const TerrainSamples<float> &samples);
[[nodiscard]] std::vector<std::uint32_t>
flattenIndices(const TerrainSamples<std::size_t> &samples);

// Writes the payload body and appends the trailing content digest.
[[nodiscard]] std::vector<std::byte>
encode(const TerrainCookedField &field, std::string &error);

// Reads a payload body. Every count is checked against the bytes that remain
// before anything is reserved, so a corrupt or hostile file produces a message
// instead of an allocation the size of the file's imagination.
[[nodiscard]] bool decode(std::span<const std::byte> bytes,
                          TerrainCookedField &field, std::string &error);

// The rules a payload must satisfy to be usable at all: a grid that agrees with
// itself, finite samples, in-range indices, and a chunk list that actually tiles
// the grid. Applied to a freshly decoded file and to a field before it is
// written, so a loaded payload can always be handed to the in-memory publisher.
[[nodiscard]] bool validate(const TerrainCookedField &field, std::string &error,
                            std::size_t *samples = nullptr);

// Color has no equality operator, so tints are compared per channel.
[[nodiscard]] bool sameColors(const std::vector<runtime::Color> &left,
                              const std::vector<runtime::Color> &right);

// Recomputed from the payload rather than trusted from the struct, so a payload
// damaged after it was written is caught instead of being served to a world that
// would then disagree with its own collider.
[[nodiscard]] bool digestMatches(const TerrainCookedField &field);

// The digest a cook of this payload should carry.
[[nodiscard]] std::uint64_t contentDigest(const TerrainCookedField &field);

} // namespace demi::runtime::cook_format
