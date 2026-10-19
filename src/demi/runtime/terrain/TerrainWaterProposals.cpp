#include "demi/runtime/terrain/TerrainWaterProposals.h"

#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace demi::runtime {
std::optional<TerrainWaterProposals> proposeTerrainBasinWater(
    const HeightField &field, const TerrainDrainage &drainage,
    std::string_view ownerId, const TerrainWaterProposalSettings &settings,
    std::stop_token stop) {
  if (ownerId.empty() || field.cellsX <= 0 || field.cellsZ <= 0 ||
      !(field.size.x > 0) || !(field.size.y > 0) ||
      !std::isfinite(field.size.x) || !std::isfinite(field.size.y) ||
      drainage.basin.size() != field.heights.size() ||
      drainage.basinSinks.size() != drainage.basinCount + 1 ||
      drainage.basinSpillLevels.size() != drainage.basinCount + 1) {
    throw std::invalid_argument(
        "Water proposals need matching prepared drainage basins");
  }
  if (!std::isfinite(settings.minimumDepth) || settings.minimumDepth <= 0 ||
      !std::isfinite(settings.minimumArea) || settings.minimumArea <= 0 ||
      !std::isfinite(settings.levelOffset) || settings.levelOffset > 0 ||
      settings.maximumBodies == 0) {
    throw std::invalid_argument("Water proposal depth/area/count must be "
                                "positive; level offset cannot be positive");
  }
  const double sampleArea =
      double(field.size.x) / field.cellsX * double(field.size.y) / field.cellsZ;
  std::vector<double> areas(drainage.basinCount + 1);
  for (std::size_t sample = 0; sample < field.heights.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    const auto basin = drainage.basin[sample];
    if (basin > drainage.basinCount) {
      throw std::invalid_argument(
          "Water proposal sample names an unknown basin");
    }
    if (basin != 0 && field.heights[sample] < drainage.basinSpillLevels[basin] +
                                                  settings.levelOffset) {
      areas[basin] += sampleArea;
    }
  }
  struct Candidate {
    std::size_t sink;
    float level;
    double area;
  };
  std::vector<Candidate> candidates;
  for (std::size_t basin = 1; basin <= drainage.basinCount; ++basin) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    const auto sink = drainage.basinSinks[basin];
    const float level = drainage.basinSpillLevels[basin] + settings.levelOffset;
    if (sink >= field.heights.size() || !std::isfinite(level)) {
      throw std::invalid_argument(
          "Water proposal basin has invalid sink/spill data");
    }
    if (double(level) - field.heights[sink] < settings.minimumDepth ||
        areas[basin] < settings.minimumArea) {
      continue;
    }
    candidates.push_back({sink, level, areas[basin]});
  }
  std::ranges::sort(candidates,
                    [](const Candidate &left, const Candidate &right) {
                      return left.area != right.area ? left.area > right.area
                                                     : left.sink < right.sink;
                    });
  TerrainWaterProposals result;
  result.water.authored = true;
  result.water.seaLevel = drainage.seaLevel;
  result.eligibleBodies = candidates.size();
  result.truncated = candidates.size() > settings.maximumBodies;
  const auto count = std::min(candidates.size(), settings.maximumBodies);
  for (std::size_t index = 0; index < count; ++index) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    const auto &candidate = candidates[index];
    const auto columns = std::size_t(field.cellsX) + 1;
    const auto x = candidate.sink % columns;
    const auto z = candidate.sink / columns;
    TerrainWaterBodySpec body;
    body.id = std::string(ownerId) + "/basin/" + std::to_string(x) + "_" +
              std::to_string(z);
    body.kind = TerrainWaterBody::Lake;
    body.level = candidate.level;
    body.center = field.position(static_cast<int>(x), static_cast<int>(z));
    body.shorelineSoftening = false;
    result.water.bodies.push_back(std::move(body));
  }
  return result;
}
} // namespace demi::runtime
