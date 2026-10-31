#include "demi/assets/TerrainPlacementCodec.h"

#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace demi::assets::terrain_payload {
namespace {
using namespace runtime;
constexpr std::size_t RecordBytes = 40;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::invalid_argument("Terrain placements: " + std::string(message));
  }
}

void append(std::vector<std::byte> &out, std::uint64_t value, unsigned width) {
  for (unsigned index = 0; index < width; ++index) {
    out.push_back(std::byte((value >> (index * 8)) & 255U));
  }
}

void validateText(std::string_view value) {
  // Keep the previous JSON payload's strict UTF-8 contract without constructing
  // an object per placement. This runs only for dictionary strings.
  try {
    static_cast<void>(nlohmann::json(value).dump());
  } catch (const nlohmann::json::exception &) {
    throw std::invalid_argument("Terrain placements: invalid UTF-8 definition");
  }
}

void appendString(std::vector<std::byte> &out, std::string_view value) {
  require(value.size() <= std::numeric_limits<std::uint32_t>::max(),
          "definition string exceeds u32 length");
  validateText(value);
  append(out, value.size(), 4);
  for (const auto character : value) {
    out.push_back(std::byte(static_cast<unsigned char>(character)));
  }
}

class Reader {
public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
  std::size_t remaining() const { return bytes_.size() - offset_; }
  std::uint64_t integer(unsigned width) {
    require(width <= remaining(), "truncated integer");
    std::uint64_t value = 0;
    for (unsigned index = 0; index < width; ++index) {
      value |= std::uint64_t(std::to_integer<unsigned>(bytes_[offset_++]))
               << (index * 8);
    }
    return value;
  }
  std::string string() {
    const auto count = integer(4);
    require(count <= remaining(), "truncated definition string");
    const auto *begin = reinterpret_cast<const char *>(bytes_.data() + offset_);
    offset_ += static_cast<std::size_t>(count);
    std::string value(begin, static_cast<std::size_t>(count));
    validateText(value);
    return value;
  }
  float scalar() {
    const auto value =
        std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
    require(std::isfinite(value), "pose must be finite");
    return value;
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

// Wire codes are independent of the native enum's declaration order.
std::uint8_t collisionCode(TerrainCollisionPolicy policy) {
  switch (policy) {
  case TerrainCollisionPolicy::None:
    return 0;
  case TerrainCollisionPolicy::Static:
    return 1;
  case TerrainCollisionPolicy::Trigger:
    return 2;
  }
  throw std::invalid_argument("Terrain placements: unknown collision policy");
}

TerrainCollisionPolicy readCollision(std::uint64_t code) {
  switch (code) {
  case 0:
    return TerrainCollisionPolicy::None;
  case 1:
    return TerrainCollisionPolicy::Static;
  case 2:
    return TerrainCollisionPolicy::Trigger;
  default:
    throw std::invalid_argument("Terrain placements: unknown collision policy");
  }
}

void validateDefinition(const TerrainScatterPlacement &value,
                        const HeightField &field) {
  require(validTerrainPlacementRuleId(value.ruleId), "invalid rule ID");
  require(value.model.empty() != value.prefab.empty(),
          "definition requires exactly one model or prefab");
  require(value.biome < field.biomeIds.size() &&
              value.biomeName == field.biomeIds[value.biome],
          "definition biome differs from field");
  require(value.collision == TerrainCollisionPolicy::None ||
              value.collision == TerrainCollisionPolicy::Static ||
              value.collision == TerrainCollisionPolicy::Trigger,
          "unknown collision policy");
}

void validateRecord(const TerrainScatterPlacement &value,
                    const HeightField &field) {
  require(value.cell < field.heights.size(), "sample cell outside field");
  require(value.candidateX >= 0 && value.candidateZ >= 0,
          "negative candidate coordinates");
  require(std::isfinite(value.position.x) && std::isfinite(value.position.y) &&
              std::isfinite(value.position.z) && std::isfinite(value.yaw) &&
              std::isfinite(value.scale) && value.scale > 0,
          "pose must be finite with positive scale");
}

using DefinitionKey =
    std::tuple<std::string_view, std::string_view, std::string_view,
               std::size_t, TerrainCollisionPolicy>;
DefinitionKey key(const TerrainScatterPlacement &value) {
  return {value.ruleId, value.model, value.prefab, value.biome,
          value.collision};
}
} // namespace

std::vector<std::byte> encodePlacements(const runtime::HeightField &field) {
  const auto *candidates =
      field.graphArtifacts && field.graphArtifacts->basePlacements
          ? field.graphArtifacts->basePlacements.get()
          : nullptr;
  std::map<DefinitionKey, std::uint32_t> indices;
  std::vector<const TerrainScatterPlacement *> definitions;
  const auto gather = [&](const auto &placements) {
    for (const auto &value : placements) {
      validateDefinition(value, field);
      validateRecord(value, field);
      if (!indices.contains(key(value))) {
        require(definitions.size() < std::numeric_limits<std::uint32_t>::max(),
                "too many definitions");
        indices.emplace(key(value),
                        static_cast<std::uint32_t>(definitions.size()));
        definitions.push_back(&value);
      }
    }
  };
  gather(field.scatterPlacements);
  if (candidates) {
    gather(*candidates);
  }
  const auto maximum = std::numeric_limits<std::size_t>::max();
  const auto finalCount = field.scatterPlacements.size();
  const auto candidateCount = candidates ? candidates->size() : 0;
  require(finalCount <= (maximum - 21) / RecordBytes &&
              candidateCount <= (maximum - 21) / RecordBytes - finalCount,
          "record bytes exceed host size");
  std::vector<std::byte> output;
  output.reserve(21 + (finalCount + candidateCount) * RecordBytes);
  append(output, definitions.size(), 4);
  append(output, field.scatterPlacements.size(), 8);
  append(output, candidates != nullptr, 1);
  append(output, candidates ? candidates->size() : 0, 8);
  for (const auto *value : definitions) {
    appendString(output, value->ruleId);
    appendString(output, value->model);
    appendString(output, value->prefab);
    append(output, value->biome, 8);
    append(output, collisionCode(value->collision), 1);
  }
  const auto records = [&](const auto &placements) {
    for (const auto &value : placements) {
      append(output, indices.at(key(value)), 4);
      append(output, value.cell, 8);
      append(output, static_cast<unsigned>(value.candidateX), 4);
      append(output, static_cast<unsigned>(value.candidateZ), 4);
      for (const float scalar : {value.position.x, value.position.y,
                                 value.position.z, value.yaw, value.scale}) {
        append(output, std::bit_cast<std::uint32_t>(scalar), 4);
      }
    }
  };
  records(field.scatterPlacements);
  if (candidates) {
    records(*candidates);
  }
  return output;
}

void decodePlacements(std::span<const std::byte> bytes,
                      runtime::HeightField &field) {
  Reader reader(bytes);
  const auto definitionCount = reader.integer(4);
  const auto scatterCount = reader.integer(8);
  const auto hasCandidates = reader.integer(1);
  const auto candidateCount = reader.integer(8);
  require(hasCandidates <= 1 && (hasCandidates || candidateCount == 0),
          "invalid graph candidate presence");
  require(!hasCandidates || field.graphArtifacts != nullptr,
          "candidates lack graph checkpoint metadata");
  require(scatterCount <= reader.remaining() / RecordBytes &&
              candidateCount <= reader.remaining() / RecordBytes - scatterCount,
          "record count exceeds section size");
  const auto recordsSize = (scatterCount + candidateCount) * RecordBytes;
  // Each definition needs three string lengths, a biome and a policy byte.
  require(definitionCount <= (reader.remaining() - recordsSize) / 21,
          "definition count exceeds section size");
  std::vector<TerrainScatterPlacement> definitions;
  definitions.reserve(static_cast<std::size_t>(definitionCount));
  for (std::uint64_t index = 0; index < definitionCount; ++index) {
    TerrainScatterPlacement value;
    value.ruleId = reader.string();
    value.model = reader.string();
    value.prefab = reader.string();
    const auto biome = reader.integer(8);
    require(biome < field.biomeIds.size(), "biome outside field");
    value.biome = static_cast<std::size_t>(biome);
    value.biomeName = field.biomeIds[value.biome];
    value.collision = readCollision(reader.integer(1));
    validateDefinition(value, field);
    definitions.push_back(std::move(value));
  }
  require(reader.remaining() == recordsSize,
          "definition/record length mismatch");
  const auto records = [&](std::uint64_t count) {
    std::vector<TerrainScatterPlacement> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      const auto definition = reader.integer(4);
      require(definition < definitions.size(), "unknown definition index");
      auto value = definitions[static_cast<std::size_t>(definition)];
      const auto cell = reader.integer(8);
      require(cell < field.heights.size(), "sample cell outside field");
      value.cell = static_cast<std::size_t>(cell);
      const auto x = reader.integer(4);
      const auto z = reader.integer(4);
      require(x <= std::numeric_limits<int>::max() &&
                  z <= std::numeric_limits<int>::max(),
              "candidate coordinates exceed integer range");
      value.candidateX = static_cast<int>(x);
      value.candidateZ = static_cast<int>(z);
      value.position = {reader.scalar(), reader.scalar(), reader.scalar()};
      value.yaw = reader.scalar();
      value.scale = reader.scalar();
      validateRecord(value, field);
      values.push_back(std::move(value));
    }
    return values;
  };
  auto scatter = records(scatterCount);
  auto candidates = records(candidateCount);
  require(reader.remaining() == 0, "unexpected trailing bytes");
  // Commit only after both lists and all definitions have been validated.
  std::shared_ptr<TerrainGraphArtifacts> artifacts;
  if (field.graphArtifacts) {
    artifacts = std::make_shared<TerrainGraphArtifacts>(*field.graphArtifacts);
    artifacts->basePlacements =
        hasCandidates
            ? std::make_shared<const std::vector<TerrainScatterPlacement>>(
                  std::move(candidates))
            : nullptr;
  }
  field.scatterPlacements = std::move(scatter);
  field.graphArtifacts = std::move(artifacts);
}
} // namespace demi::assets::terrain_payload
