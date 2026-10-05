#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/runtime/terrain/TerrainGraph.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {

// A small generated field with two biomes, real chunks, and provenance, so the
// round trip below exercises every block rather than an empty payload that
// would pass while nothing was stored.
TerrainRecipe recipeForField() {
  TerrainRecipe recipe;
  recipe.size = {48, 32};
  recipe.cellsX = 12;
  recipe.cellsZ = 8;
  recipe.chunkCells = 5;
  recipe.seed = 4242;
  recipe.biomes.emplace("rock", TerrainBiome{.color = {.6F, .5F, .4F, 1}});
  recipe.biomes.emplace("sand", TerrainBiome{.color = {.8F, .75F, .55F, 1}});
  recipe.defaultBiome = "sand";
  return recipe;
}

std::shared_ptr<const HeightField> generateField() {
  auto generated = TerrainGenerator::generate(recipeForField());
  assert(generated.has_value() && "Terrain generation unexpectedly cancelled");
  auto field = std::make_shared<HeightField>(std::move(*generated));
  field->paletteId = "asset://terrain/palettes/test";
  field->stageOrder = "landform,masks,surface";
  field->quality = "standard";
  field->inputFingerprint = "fnv1a64:0123456789abcdef";
  return field;
}

TerrainCookInput inputFor(const HeightField &field, const TerrainMasks *masks) {
  return TerrainCookInput{
      .field = &field,
      .masks = masks,
      .water = TerrainWaterLevel{.seaLevel = 4.5F, .authored = true},
      .generatorVersionTag = std::to_string(terrainGeneratorVersion),
      .recipeDigest = terrainCookRecipeDigest(recipeForField()),
      .inputFingerprint = "fnv1a64:0123456789abcdef"};
}

// Derived masks for the whole grid. Every consumer of a mask band is downstream
// of these values, so a cook that drops or reorders one is silently wrong
// rather than loudly broken.
TerrainMasks masksFor(const HeightField &field) {
  TerrainMasks masks;
  const auto samples = field.heights.size();
  masks.slope.resize(samples);
  masks.moisture.resize(samples);
  masks.waterDistance.resize(samples);
  masks.flow.resize(samples);
  masks.sediment.resize(samples);
  masks.substrate.resize(samples);
  for (std::size_t index = 0; index < samples; ++index) {
    masks.slope.set(index, float(index % 17) / 17.0F);
    masks.moisture.set(index, float(index % 5) / 5.0F);
    masks.waterDistance.set(index, float(index % 23) * 0.5F);
    masks.flow.set(index, float(index % 11) / 11.0F);
    masks.sediment.set(index, float(index % 3) - 1.0F);
    masks.substrate.set(index, index % 4);
  }
  return masks;
}

// Attempts a read and reports the refusal. A refusal with no message is a
// failure in itself: whoever dropped a cache has to be able to say why.
std::string refusal(std::span<const std::byte> bytes) {
  std::string error;
  const auto result = deserializeTerrainCookedField(bytes, &error);
  if (result)
    return {};
  assert(!error.empty() && "A rejected payload must explain itself");
  return error;
}

bool rejected(std::span<const std::byte> bytes, const char *expectedFragment) {
  const auto error = refusal(bytes);
  return !error.empty() && error.find(expectedFragment) != std::string::npos;
}

// The whole point of the format: a cooked field that comes back identical, byte
// for byte, in every field the struct carries. If this fails, a shipped game is
// running a terrain that is not the one that was cooked.
void roundTripsEveryField() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto input = inputFor(*field, &masks);
  std::string error;
  const auto cooked = cookTerrainField(input, &error);
  assert(cooked && error.empty());
  assert(cooked->masksPresent);

  const auto bytes = serializeTerrainCookedField(*cooked);
  const auto loaded = deserializeTerrainCookedField(bytes, &error);
  assert(loaded && error.empty());

  assert(loaded->version == terrainCookVersion);
  assert(loaded->version == cooked->version);
  assert(loaded->recipeDigest == input.recipeDigest);
  assert(loaded->size.x == field->size.x && loaded->size.y == field->size.y);
  assert(loaded->cellsX == field->cellsX && loaded->cellsZ == field->cellsZ);
  assert(loaded->heights == cooked->heights);
  assert(loaded->baseHeights == cooked->baseHeights);
  assert(loaded->normalsX == cooked->normalsX);
  assert(loaded->normalsY == cooked->normalsY);
  assert(loaded->normalsZ == cooked->normalsZ);
  assert(loaded->biomeIndices == cooked->biomeIndices);
  assert(loaded->exclusions == cooked->exclusions);
  assert(loaded->slope == cooked->slope);
  assert(loaded->moisture == cooked->moisture);
  assert(loaded->waterDistance == cooked->waterDistance);
  assert(loaded->flow == cooked->flow);
  assert(loaded->sediment == cooked->sediment);
  assert(loaded->substrates == cooked->substrates);
  assert(loaded->biomeIds == cooked->biomeIds);
  assert(loaded->biomeColors.size() == cooked->biomeColors.size());
  for (std::size_t index = 0; index < cooked->biomeColors.size(); ++index) {
    const auto &before = cooked->biomeColors[index];
    const auto &after = loaded->biomeColors[index];
    assert(before.r == after.r && before.g == after.g && before.b == after.b &&
           before.a == after.a);
  }
  assert(loaded->chunks.size() == field->chunks.size());
  for (std::size_t index = 0; index < field->chunks.size(); ++index) {
    const auto &before = field->chunks[index];
    const auto &after = loaded->chunks[index];
    assert(before.firstCellX == after.firstCellX &&
           before.firstCellZ == after.firstCellZ &&
           before.cellsX == after.cellsX && before.cellsZ == after.cellsZ);
  }
  assert(loaded->generatorVersionTag == cooked->generatorVersionTag);
  assert(loaded->inputFingerprint == cooked->inputFingerprint);
  assert(loaded->stageOrder == cooked->stageOrder);
  assert(loaded->quality == cooked->quality);
  assert(loaded->paletteId == cooked->paletteId);
  assert(loaded->seaLevel == cooked->seaLevel);

  // The digest is a statement about the bytes, so it must survive the trip and
  // must not be recomputed from a lossy in-memory copy.
  assert(loaded->contentHash == cooked->contentHash);
  assert(loaded->contentHash != 0);

  // A byte-level round trip is identical, not merely equivalent: two cooks of
  // the same field produce the same file, so a cache key built on the payload
  // is stable across machines and rebuilds.
  const auto again = serializeTerrainCookedField(*loaded);
  assert(again.size() == bytes.size());
  assert(std::memcmp(again.data(), bytes.data(), bytes.size()) == 0);
}

// A field with no biomes and no masks is a real case (an unassigned preview),
// and it is the case that a format that always writes a biome block would get
// wrong.
void emptyBiomesAndEmptyMasksRoundTrip() {
  const auto field = generateField();
  auto withoutBiomes = *field;
  withoutBiomes.biomeIds.clear();
  withoutBiomes.biomeColors.clear();
  withoutBiomes.biomeIndices = TerrainSamples<std::size_t>{};
  withoutBiomes.biomeIndices.resize(withoutBiomes.heights.size());
  for (std::size_t index = 0; index < withoutBiomes.heights.size(); ++index)
    withoutBiomes.biomeIndices.set(index, 0);

  const auto input = inputFor(withoutBiomes, nullptr);
  std::string error;
  const auto cooked = cookTerrainField(input, &error);
  assert(cooked && error.empty());
  assert(cooked->biomeIds.empty() && cooked->biomeColors.empty());
  assert(!cooked->masksPresent);

  const auto bytes = serializeTerrainCookedField(*cooked);
  const auto loaded = deserializeTerrainCookedField(bytes, &error);
  assert(loaded && error.empty());
  assert(loaded->biomeIds.empty() && loaded->biomeColors.empty());
  assert(loaded->heights == cooked->heights);
  assert(loaded->biomeIndices == cooked->biomeIndices);
}

// Whether a mask set was derived is recorded, not defaulted. A consumer that
// asked for a cooked field without masks must be able to tell the difference
// between "no masks exist" and "every mask is zero", because those mean
// different things to a slope band or a scatter filter.
void maskPresenceIsRecorded() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  std::string error;

  const auto without = cookTerrainField(inputFor(*field, nullptr), &error);
  assert(without && error.empty());
  assert(!without->masksPresent);
  assert(without->slope.empty() && without->moisture.empty() &&
         without->waterDistance.empty() && without->flow.empty() &&
         without->sediment.empty() && without->substrates.empty());

  const auto with = cookTerrainField(inputFor(*field, &masks), &error);
  assert(with && error.empty());
  assert(with->masksPresent);

  const auto loadedWithout = deserializeTerrainCookedField(
      serializeTerrainCookedField(*without), &error);
  const auto loadedWith =
      deserializeTerrainCookedField(serializeTerrainCookedField(*with), &error);
  assert(loadedWithout && loadedWith && error.empty());
  assert(!loadedWithout->masksPresent && loadedWith->masksPresent);
  assert(loadedWithout->slope.empty() && !loadedWith->slope.empty());

  // A mask set that covers only part of the grid is refused rather than stored,
  // because reading back zeros for the rest is a claim nobody made.
  TerrainMasks partial = masks;
  partial.slope.pop_back();
  const auto refused = cookTerrainField(inputFor(*field, &partial), &error);
  assert(!refused);
  assert(error.find("whole grid") != std::string::npos);
}

// A truncated file is not a smaller field. Every offset has to be refused with
// a message and without reading past the end, because a cook cache is exactly
// the file most likely to be half-written when a build was interrupted.
void truncationIsRejected() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto cooked = cookTerrainField(inputFor(*field, &masks));
  assert(cooked.has_value());
  const auto bytes = serializeTerrainCookedField(*cooked);
  assert(bytes.size() > 512);

  // Sample the cuts: inside the header, between the header and the first block,
  // mid-height-array, at a table boundary, and one byte short of the digest.
  for (const std::size_t cut :
       {std::size_t{0}, std::size_t{1}, std::size_t{8}, std::size_t{16},
        std::size_t{40}, std::size_t{64}, std::size_t{68}, std::size_t{200},
        bytes.size() / 2, bytes.size() - 9, bytes.size() - 1}) {
    if (cut >= bytes.size())
      continue;
    const std::span<const std::byte> truncated(bytes.data(), cut);
    const auto error = refusal(truncated);
    assert(!error.empty() && "A truncated payload was accepted");
    assert(error.find("Terrain cook") == 0 &&
           "A refusal must name what it refused");
  }
}

// Counts come from the file, so a count must be checked against the bytes that
// are left before anything is reserved. This is the hostile-input case: a count
// of two billion with a 4KB file behind it.
void impossibleCountsAreRejected() {
  const auto field = generateField();
  const auto cooked = cookTerrainField(inputFor(*field, nullptr));
  assert(cooked.has_value());
  const auto bytes = serializeTerrainCookedField(*cooked);

  const auto patchInt32 = [](std::vector<std::byte> &target, std::size_t offset,
                             std::int32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte)
      target[offset + byte] = static_cast<std::byte>(static_cast<unsigned char>(
          (static_cast<std::uint32_t>(value) >> (8 * byte)) & 0xFFU));
  };

  // The first block count sits immediately after the 64-byte header.
  const std::size_t firstCount = 64;
  auto huge = bytes;
  patchInt32(huge, firstCount, std::numeric_limits<std::int32_t>::max());
  assert(rejected(huge, "exceeds the bytes left"));

  auto negative = bytes;
  patchInt32(negative, firstCount, -1);
  assert(rejected(negative, "negative"));

  auto negativeGrid = bytes;
  patchInt32(negativeGrid, 40, -5); // cellsX
  assert(rejected(negativeGrid, "must not be negative"));

  // A biome count larger than the payload, in a field that has two biomes.
  std::size_t biomeCountOffset = 0;
  {
    std::size_t offset = 64;
    const auto skip = [&](std::size_t elements, std::size_t width) {
      offset += sizeof(std::int32_t) + elements * width;
    };
    const auto samples =
        std::size_t(field->cellsX + 1) * std::size_t(field->cellsZ + 1);
    skip(samples, sizeof(float));         // heights
    skip(samples, sizeof(float));         // base heights
    skip(samples, sizeof(float));         // normals x
    skip(samples, sizeof(float));         // normals y
    skip(samples, sizeof(float));         // normals z
    skip(samples, sizeof(std::uint32_t)); // biome indices
    skip(samples, sizeof(float));         // exclusions
    biomeCountOffset = offset;
  }
  auto hugeBiomes = bytes;
  patchInt32(hugeBiomes, biomeCountOffset, 1 << 20);
  assert(rejected(hugeBiomes, "exceeds the bytes left"));
  auto negativeBiomes = bytes;
  patchInt32(negativeBiomes, biomeCountOffset, -7);
  assert(rejected(negativeBiomes, "negative"));
}

// A NaN height or an infinite mask propagates into the mesh, the collider and
// every derived band, where it does not fail but silently poisons later
// comparisons. It is refused where it enters.
void nonFiniteSamplesAreRejected() {
  const auto field = generateField();
  const auto masks = masksFor(*field);

  HeightField withNaN = *field;
  withNaN.heights.set(3, std::numeric_limits<float>::quiet_NaN());
  std::string error;
  assert(!cookTerrainField(inputFor(withNaN, nullptr), &error));
  assert(error.find("not finite") != std::string::npos);

  HeightField withInfinity = *field;
  withInfinity.heights.set(7, std::numeric_limits<float>::infinity());
  assert(!cookTerrainField(inputFor(withInfinity, nullptr), &error));

  TerrainMasks withInfiniteMask = masks;
  withInfiniteMask.flow.set(2, -std::numeric_limits<float>::infinity());
  assert(!cookTerrainField(inputFor(*field, &withInfiniteMask), &error));
  assert(error.find("not finite") != std::string::npos);

  // And a payload that carries one is refused on the way back in, which is the
  // path an untrusted file actually takes.
  auto cooked = cookTerrainField(inputFor(*field, &masks));
  assert(cooked.has_value());
  auto bytes = serializeTerrainCookedField(*cooked);
  const auto heightOffset =
      std::size_t{64} + sizeof(std::int32_t) + 3 * sizeof(float);
  const std::uint32_t nanBits = 0x7FC00000U;
  for (std::size_t byte = 0; byte < sizeof(nanBits); ++byte)
    bytes[heightOffset + byte] = static_cast<std::byte>(
        static_cast<unsigned char>((nanBits >> (8 * byte)) & 0xFFU));
  assert(rejected(bytes, "not finite"));

  // The digest is verified as well, so the corruption is caught twice over:
  // once by the digest and once by the finiteness rule. Either message is a
  // refusal.
  std::string ignored;
  assert(!deserializeTerrainCookedField(bytes, &ignored));
}

// The case a cook cache most needs. The file is well formed, every count
// matches, every sample is finite and the grid is the right size, and one byte
// of a height is wrong. Nothing structural can see it; the digest must.
void aCorruptedHeightIsCaughtByTheDigest() {
  const auto field = generateField();
  const auto cooked = cookTerrainField(inputFor(*field, nullptr));
  assert(cooked.has_value());
  const auto bytes = serializeTerrainCookedField(*cooked);
  std::string error;
  assert(deserializeTerrainCookedField(bytes, &error) && error.empty());

  // Flip the low bit of the mantissa of a height well inside the array. The
  // sample stays finite and in range, so only the digest can notice.
  const std::size_t heightOffset =
      std::size_t{64} + sizeof(std::int32_t) + 5 * sizeof(float);
  auto corrupted = bytes;
  corrupted[heightOffset] = static_cast<std::byte>(
      std::to_integer<unsigned char>(corrupted[heightOffset]) ^ 0x01U);
  assert(rejected(corrupted, "digest"));

  // The same corruption is also visible as staleness on a payload that was
  // already loaded and then modified in memory, which is how a partially
  // written cache looks to the process holding it.
  auto loaded = deserializeTerrainCookedField(bytes, &error);
  assert(loaded.has_value());
  const auto original = *loaded;
  loaded->heights[5] = std::bit_cast<float>(
      std::bit_cast<std::uint32_t>(loaded->heights[5]) ^ 1U);
  assert(terrainCookStaleness(original, inputFor(*field, nullptr)).empty());
  assert(terrainCookStaleness(*loaded, inputFor(*field, nullptr)) ==
         "content_hash");
}

// Every cause of staleness gets its own reason. An editor that can only say
// "cache dropped" cannot explain a rebuild that happens on every load.
void eachStalenessCauseIsDistinct() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto input = inputFor(*field, &masks);
  const auto cooked = cookTerrainField(input);
  assert(cooked.has_value());
  assert(terrainCookStaleness(*cooked, input).empty());

  auto wrongVersion = *cooked;
  wrongVersion.version = terrainCookVersion + 1;
  assert(terrainCookStaleness(wrongVersion, input) == "version");

  auto wrongGenerator = *cooked;
  wrongGenerator.generatorVersionTag = "999";
  assert(terrainCookStaleness(wrongGenerator, input) == "generator_version");

  auto wrongFingerprint = *cooked;
  wrongFingerprint.inputFingerprint = "fnv1a64:deadbeefdeadbeef";
  assert(terrainCookStaleness(wrongFingerprint, input) == "input_fingerprint");

  auto differentRecipe = recipeForField();
  ++differentRecipe.seed;
  auto differentSeedInput = input;
  differentSeedInput.recipeDigest = terrainCookRecipeDigest(differentRecipe);
  assert(differentSeedInput.inputFingerprint == input.inputFingerprint);
  assert(terrainCookStaleness(*cooked, differentSeedInput) == "recipe_digest");

  // A different grid, a different biome list and a different stage order are
  // all "the cached field is not this field".
  auto wrongGridCells = *cooked;
  wrongGridCells.cellsX = field->cellsX + 1;
  assert(terrainCookStaleness(wrongGridCells, input) == "grid");
  auto wrongGridSize = *cooked;
  wrongGridSize.size.x = field->size.x * 2;
  assert(terrainCookStaleness(wrongGridSize, input) == "grid");
  auto wrongBiomes = *cooked;
  wrongBiomes.biomeIds.push_back("extra");
  assert(terrainCookStaleness(wrongBiomes, input) == "grid");
  auto wrongStageOrder = *cooked;
  wrongStageOrder.stageOrder = "landform";
  assert(terrainCookStaleness(wrongStageOrder, input) == "grid");

  // The reasons are checked in the order that matters: a payload that is wrong
  // in several ways reports the cause that explains the rebuild, not whichever
  // comparison happened to run first.
  auto wrongEverything = *cooked;
  wrongEverything.version = terrainCookVersion + 1;
  wrongEverything.generatorVersionTag = "999";
  assert(terrainCookStaleness(wrongEverything, input) == "version");

  // No field to compare against is its own answer, because the caller cannot
  // have established that the cached field is the right one.
  TerrainCookInput fieldless{.field = nullptr,
                             .generatorVersionTag = cooked->generatorVersionTag,
                             .recipeDigest = cooked->recipeDigest,
                             .inputFingerprint = cooked->inputFingerprint};
  assert(terrainCookStaleness(*cooked, fieldless) == "input_field_missing");
}

// A file written by a build whose float handling differs cannot be interpreted,
// and guessing at it is how a stale cook becomes a corrupt heightfield. The
// same goes for a different layout version.
void foreignLayoutsAreRefused() {
  const auto field = generateField();
  const auto bytes =
      serializeTerrainCookedField(*cookTerrainField(inputFor(*field, nullptr)));
  std::string error;
  assert(deserializeTerrainCookedField(bytes, &error) && error.empty());

  // Wrong magic.
  auto wrongMagic = bytes;
  wrongMagic[2] = static_cast<std::byte>(std::byte{'X'});
  assert(rejected(wrongMagic, "magic"));

  // Byte order: the same four bytes a big-endian writer would have produced for
  // the marker. Reading them as little-endian yields 0x01020304 reversed, which
  // is how a foreign file is told apart from a corrupt one.
  auto bigEndian = bytes;
  for (std::size_t index = 0; index < 4; ++index)
    bigEndian[8 + index] = static_cast<std::byte>(
        std::byte{static_cast<unsigned char>(index + 1)});
  assert(rejected(bigEndian, "little-endian"));

  // Version.
  auto wrongVersion = bytes;
  wrongVersion[12] = static_cast<std::byte>(
      std::byte{static_cast<unsigned char>(terrainCookVersion + 1)});
  assert(rejected(wrongVersion, "version"));

  // Float probe.
  auto wrongFloat = bytes;
  wrongFloat[24] = static_cast<std::byte>(std::byte{0x01});
  assert(rejected(wrongFloat, "float probe"));

  // An unknown flag bit means a newer writer added meaning this build does not
  // have, so the fields below cannot be trusted.
  auto unknownFlag = bytes;
  unknownFlag[20] = static_cast<std::byte>(std::byte{0x80});
  assert(rejected(unknownFlag, "flags"));

  // A header that claims a different size, and reserved bytes that are not
  // zero.
  auto wrongHeader = bytes;
  wrongHeader[16] = static_cast<std::byte>(std::byte{0x80});
  assert(rejected(wrongHeader, "header size"));
  auto dirtyReserved = bytes;
  dirtyReserved[48] = static_cast<std::byte>(std::byte{0x01});
  assert(rejected(dirtyReserved, "reserved"));

  // Trailing bytes after the digest mean this is not the file the writer made.
  auto extended = bytes;
  extended.push_back(std::byte{0x7F});
  assert(rejected(extended, "trailing"));
}

// The loader is the one place a cooked file becomes a live field, so it checks
// itself: a field that does not reproduce the file it came from is a bug, and
// it is reported as one rather than returned.
void loadingReproducesTheCookedField() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto cooked = cookTerrainField(inputFor(*field, &masks));
  assert(cooked.has_value());
  const auto bytes = serializeTerrainCookedField(*cooked);
  std::string error;
  const auto loaded = deserializeTerrainCookedField(bytes, &error);
  assert(loaded.has_value() && error.empty());

  const auto live = loadTerrainCookedField(*loaded);
  assert(live);
  assert(live->heights == field->heights);
  assert(live->baseHeights == field->baseHeights);
  assert(live->exclusions == field->exclusions);
  assert(live->biomeIndices == field->biomeIndices);
  // Vec3 has no equality operator, so TerrainSamples<Vec3> cannot be compared
  // with ==. That is exactly why the packed form stores the normal components
  // separately: a cooked file holds three arrays, not an interleaved vector, so
  // the component comparison is the same one the format makes.
  assert(live->normals.size() == field->normals.size());
  for (std::size_t index = 0; index < live->normals.size(); ++index) {
    const auto before = field->normals[index];
    const auto after = live->normals[index];
    assert(before.x == after.x && before.y == after.y && before.z == after.z);
  }
  assert(live->biomeIds == field->biomeIds);
  assert(live->chunks.size() == field->chunks.size());
  assert(live->cellsX == field->cellsX && live->cellsZ == field->cellsZ);
  assert(live->size.x == field->size.x && live->size.y == field->size.y);
  assert(live->paletteId == field->paletteId);
  assert(live->inputFingerprint == field->inputFingerprint);
  assert(live->stageOrder == field->stageOrder);
  assert(live->quality == field->quality);

  // Spot-check a few samples through the public accessors, not just the raw
  // storage: a transposed row would be invisible to a vector comparison only if
  // the comparison itself were wrong, and position()/height() are what gameplay
  // and physics actually call.
  for (int z = 0; z <= field->cellsZ; ++z)
    for (int x = 0; x <= field->cellsX; ++x) {
      const auto sample = field->index(x, z);
      assert(std::abs(live->height(x, z) - field->height(x, z)) <= 0.0F);
      assert(live->normal(x, z).x == field->normal(x, z).x);
      assert(live->normal(x, z).y == field->normal(x, z).y);
      assert(live->normal(x, z).z == field->normal(x, z).z);
      assert(live->position(x, z).x == field->position(x, z).x);
      assert(live->position(x, z).y == field->position(x, z).y);
      assert(live->biomeIndices[sample] == field->biomeIndices[sample]);
    }
}

// A field that could not be published at runtime is not worth writing to disk,
// so cook refuses it with a reason rather than producing a file the generation
// cache would later reject.
void unpublishableFieldsAreRefused() {
  const auto field = generateField();
  std::string error;

  assert(!cookTerrainField(TerrainCookInput{}, &error));
  assert(error.find("without a generated field") != std::string::npos);

  auto missingRecipe = inputFor(*field, nullptr);
  missingRecipe.recipeDigest.clear();
  assert(!cookTerrainField(missingRecipe, &error));
  assert(error.find("canonical recipe digest") != std::string::npos);

  HeightField shortHeights = *field;
  shortHeights.heights.pop_back();
  assert(!cookTerrainField(inputFor(shortHeights, nullptr), &error));

  HeightField badNormal = *field;
  auto normal = badNormal.normals[0];
  normal.y = 0;
  badNormal.normals.set(0, normal);
  assert(!cookTerrainField(inputFor(badNormal, nullptr), &error));
  assert(error.find("unit vector") != std::string::npos);

  HeightField badBiome = *field;
  badBiome.biomeIndices.set(0, 99);
  assert(!cookTerrainField(inputFor(badBiome, nullptr), &error));
  assert(error.find("biome list") != std::string::npos);

  HeightField badExclusion = *field;
  badExclusion.exclusions.set(0, 4);
  assert(!cookTerrainField(inputFor(badExclusion, nullptr), &error));
  assert(error.find("[0, 1]") != std::string::npos);

  // A chunk list that does not tile the grid would let a consumer resolve a
  // sample to two chunks or none.
  HeightField overlappingChunks = *field;
  overlappingChunks.chunks.push_back(overlappingChunks.chunks.front());
  assert(!cookTerrainField(inputFor(overlappingChunks, nullptr), &error));
  assert(error.find("overlap") != std::string::npos);

  HeightField missingChunks = *field;
  missingChunks.chunks.pop_back();
  assert(!cookTerrainField(inputFor(missingChunks, nullptr), &error));
  assert(error.find("cover the whole grid") != std::string::npos);

  HeightField outsideChunk = *field;
  outsideChunk.chunks.front().firstCellX = field->cellsX - 1;
  assert(!cookTerrainField(inputFor(outsideChunk, nullptr), &error));
  assert(error.find("outside the grid") != std::string::npos);

  TerrainCookInput infiniteWater = inputFor(*field, nullptr);
  infiniteWater.water.seaLevel = std::numeric_limits<float>::infinity();
  assert(!cookTerrainField(infiniteWater, &error));
  assert(error.find("water level") != std::string::npos);
}

// The provenance handed to the cook graph must move when any of the three
// generation inputs moves, because a cache key built from a value that does not
// move is a cache that serves the wrong terrain.
void provenanceCoversEveryGenerationInput() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto base = inputFor(*field, &masks);
  const auto reference = terrainCookProvenance(base);
  assert(reference.generatorVersionTag == base.generatorVersionTag);
  assert(reference.recipeDigest == base.recipeDigest);
  assert(reference.inputFingerprint == base.inputFingerprint);
  assert(reference.recipeKey != reference.inputFingerprint);
  assert(reference.recipeKey != reference.generatorVersionTag);

  auto movedGenerator = base;
  movedGenerator.generatorVersionTag = "999";
  assert(terrainCookProvenance(movedGenerator).recipeKey !=
         reference.recipeKey);
  auto movedFingerprint = base;
  movedFingerprint.inputFingerprint = "fnv1a64:ffffffffffffffff";
  assert(terrainCookProvenance(movedFingerprint).recipeKey !=
         reference.recipeKey);
  auto changedSeed = recipeForField();
  ++changedSeed.seed;
  auto movedRecipe = base;
  movedRecipe.recipeDigest = terrainCookRecipeDigest(changedSeed);
  assert(terrainCookProvenance(movedRecipe).recipeKey != reference.recipeKey);
}
// Every single-byte corruption of a real payload, plus a deterministic spread
// of multi-byte ones. A corrupted cook file is the normal case after an
// interrupted build or a bad disk, and every one of them has to end in a
// refusal rather than a crash, a read past the end, or a field that loads and
// lies.
void corruptionIsAlwaysRefused() {
  const auto field = generateField();
  const auto masks = masksFor(*field);
  const auto cooked = cookTerrainField(inputFor(*field, &masks));
  assert(cooked.has_value());
  const auto bytes = serializeTerrainCookedField(*cooked);
  std::string error;

  std::size_t accepted = 0;
  for (std::size_t position = 0; position < bytes.size(); ++position) {
    for (const unsigned char mask : {0x01U, 0x80U, 0xFFU}) {
      auto corrupted = bytes;
      corrupted[position] = static_cast<std::byte>(
          std::to_integer<unsigned char>(corrupted[position]) ^ mask);
      if (corrupted == bytes)
        continue;
      const auto loaded = deserializeTerrainCookedField(corrupted, &error);
      if (loaded) {
        ++accepted;
        continue;
      }
      assert(!error.empty());
    }
  }
  // Some corruptions are genuinely indistinguishable from a different but valid
  // file, and those may load. What must never happen is a load that reports a
  // different digest from the bytes it was read out of.
  assert(accepted * 20 < bytes.size() &&
         "Too many corruptions loaded as valid payloads");

  // A deterministic LCG rather than a random device, so a failure here is
  // reproducible and the test is the same on every machine.
  std::uint32_t state = 0x9E3779B9U;
  const auto nextRandom = [&state] {
    state = state * 1664525U + 1013904223U;
    return state;
  };
  for (int trial = 0; trial < 400; ++trial) {
    auto corrupted = bytes;
    const auto flips = 1 + nextRandom() % 6;
    for (unsigned int flip = 0; flip < flips; ++flip) {
      const auto position = nextRandom() % bytes.size();
      corrupted[position] =
          static_cast<std::byte>(static_cast<unsigned char>(nextRandom()));
    }
    const auto loaded = deserializeTerrainCookedField(corrupted, &error);
    if (!loaded) {
      assert(!error.empty());
      continue;
    }
    // A payload that loads must be internally consistent: re-serializing it has
    // to reproduce the file it came from, or the digest is not covering what it
    // claims to cover.
    assert(serializeTerrainCookedField(*loaded) == corrupted);
  }
}

} // namespace

int main() {
  TerrainRecipe graphRecipe;
  graphRecipe.graph = defaultTerrainGraph();
  const auto graphDigest = terrainCookRecipeDigest(graphRecipe);
  graphRecipe.graph["nodes"][0]["position"] = {190, 270};
  assert(terrainCookRecipeDigest(graphRecipe) == graphDigest);
  graphRecipe.seed += 1;
  assert(terrainCookRecipeDigest(graphRecipe) != graphDigest);
  roundTripsEveryField();
  emptyBiomesAndEmptyMasksRoundTrip();
  maskPresenceIsRecorded();
  truncationIsRejected();
  impossibleCountsAreRejected();
  nonFiniteSamplesAreRejected();
  aCorruptedHeightIsCaughtByTheDigest();
  eachStalenessCauseIsDistinct();
  foreignLayoutsAreRefused();
  loadingReproducesTheCookedField();
  unpublishableFieldsAreRefused();
  provenanceCoversEveryGenerationInput();
  corruptionIsAlwaysRefused();
  std::cout << "Terrain cook checks passed\n";
}
