#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace demi::runtime;

namespace {
nlohmann::json readDocument(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Cannot read terrain source: " + path.string());
  }
  return nlohmann::json::parse(input);
}

TerrainRecipe readRecipe(const std::filesystem::path &path) {
  auto document = readDocument(path);
  if (document.value("importer", "") == "terrain_heightfield") {
    document = readDocument(path.parent_path() /
                            document.at("source").get<std::string>());
  }
  return TerrainRecipe::parse(
      document.contains("recipe") ? document.at("recipe") : document);
}

std::string heightDigest(const TerrainSamples<float> &heights) {
  // Explicit little-endian IEEE float bytes make the digest independent of
  // host byte order. This is evidence of repeatability, not a cryptographic ID.
  std::uint64_t digest = 14695981039346656037ULL;
  for (std::size_t sample = 0; sample < heights.size(); ++sample) {
    const auto bits = std::bit_cast<std::uint32_t>(heights[sample]);
    for (unsigned shift = 0; shift < 32; shift += 8) {
      digest ^= (bits >> shift) & 255;
      digest *= 1099511628211ULL;
    }
  }
  std::ostringstream text;
  text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16)
       << digest;
  return text.str();
}

nlohmann::json sedimentSummary(const HeightField &field) {
  auto summaries = nlohmann::json::object();
  if (!field.graphArtifacts) {
    return summaries;
  }
  for (const auto &[node, balance] : field.graphArtifacts->sedimentBalance) {
    double deposited = 0;
    double removed = 0;
    float minimum = 0;
    float maximum = 0;
    for (std::size_t sample = 0; sample < balance->size(); ++sample) {
      const float value = (*balance)[sample];
      minimum = std::min(minimum, value);
      maximum = std::max(maximum, value);
      if (value > 0) {
        deposited += value;
      } else {
        removed -= value;
      }
    }
    summaries[node] = {{"deposition", deposited},
                       {"cut", removed},
                       {"net", deposited - removed},
                       {"minimum", minimum},
                       {"maximum", maximum}};
  }
  return summaries;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      throw std::invalid_argument("Usage: benchmark-terrain-generation SOURCE "
                                  "[--geometry-only] [--repeat]");
    }
    bool geometryOnly = false;
    int repetitions = 1;
    for (int argument = 2; argument < argc; ++argument) {
      const std::string option = argv[argument];
      if (option == "--geometry-only") {
        geometryOnly = true;
      } else if (option == "--repeat") {
        repetitions = 2;
      } else {
        throw std::invalid_argument("Unknown option: " + option);
      }
    }
    auto recipe = readRecipe(argv[1]);
    if (!recipe.paletteId.empty() && !geometryOnly) {
      throw std::invalid_argument(
          "Palette assets require project resolution; use --geometry-only to "
          "explicitly exclude palette/scatter dependencies from this probe.");
    }
    if (geometryOnly) {
      recipe.paletteId.clear();
    }
    nlohmann::json report = {
        {"source", argv[1]},
        {"generator_version", terrainGeneratorVersion},
        {"geometry_only", geometryOnly},
        {"resolution", {recipe.cellsX, recipe.cellsZ}},
        {"note",
         geometryOnly
             ? "Palette cleared; unresolved palette scatter produces no "
               "placements. Original grid and authored geometry retained."
             : "No palette dependencies."},
        {"runs", nlohmann::json::array()}};
    for (const auto quality :
         {TerrainQuality::Preview, TerrainQuality::Standard}) {
      std::string previousDigest;
      for (int repetition = 0; repetition < repetitions; ++repetition) {
        std::size_t callbacks = 0;
        const auto started = std::chrono::steady_clock::now();
        const auto field = TerrainGenerator::generate(
            recipe, {}, [&](float) { ++callbacks; }, quality);
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        if (!field) {
          throw std::runtime_error("Terrain generation returned no field");
        }
        const auto digest = heightDigest(field->heights);
        if (!previousDigest.empty() && previousDigest != digest) {
          throw std::runtime_error("Repeated generation changed height digest");
        }
        previousDigest = digest;
        report["runs"].push_back({{"quality", terrainQualityName(quality)},
                                  {"repetition", repetition + 1},
                                  {"milliseconds", elapsed},
                                  {"sample_count", field->heights.size()},
                                  {"progress_callbacks", callbacks},
                                  {"height_digest", digest},
                                  {"sediment", sedimentSummary(*field)}});
      }
    }
    std::cout << report.dump(2) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
