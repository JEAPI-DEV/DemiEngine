#include "demi/runtime/terrain/TerrainErosion.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

void selectSourceNode(TerrainRecipe &recipe, const std::string &sourceNode) {
  auto graph = TerrainGraph::parse(recipe.graph);
  if (!graph.node(sourceNode)) {
    throw std::invalid_argument("Unknown source node: " + sourceNode);
  }
  std::erase_if(graph.links, [&](const auto &link) {
    return link.to.node == graph.output && link.to.port != "field";
  });
  auto output = std::ranges::find_if(graph.links, [&](const auto &link) {
    return link.to.node == graph.output && link.to.port == "field";
  });
  if (output == graph.links.end()) {
    graph.links.push_back({"comparison_source_to_output",
                           {sourceNode, "field"},
                           {graph.output, "field"}});
  } else {
    output->from = {sourceNode, "field"};
  }
  recipe.graph = TerrainGraph::parse(graph.toJson()).toJson();
}

void writeHeights(const std::filesystem::path &path,
                  const TerrainSamples<float> &heights) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Cannot write comparison samples: " +
                             path.string());
  }
  for (const float height : heights) {
    const auto bits = std::bit_cast<std::uint32_t>(height);
    for (unsigned shift = 0; shift < 32; shift += 8) {
      output.put(static_cast<char>((bits >> shift) & 255));
    }
  }
  if (!output) {
    throw std::runtime_error("Comparison sample write failed: " +
                             path.string());
  }
}

HeightField analyticField(bool spike, bool anisotropic) {
  HeightField field;
  field.cellsX = field.cellsZ = 16;
  field.size = anisotropic ? Vec2{64, 16} : Vec2{16, 16};
  field.heights.resize(17 * 17);
  field.baseHeights.resize(17 * 17);
  for (int z = 0; z <= 16; ++z) {
    for (int x = 0; x <= 16; ++x) {
      const auto position = field.position(x, z);
      const double centreX = field.size.x / 2;
      const double distance = std::hypot(position.x - centreX, position.y - 8);
      const double pit = distance <= 3 ? 10 * (distance * distance / 9 - 1)
                                       : -2 * (distance - 3);
      const double peak = distance < 2 ? 30 * (1 - distance / 2) : 0;
      const float height =
          spike ? float(0.35 * (position.x + position.y) + pit + peak) : 5.F;
      field.heights.set(field.index(x, z), height);
      field.baseHeights.set(field.index(x, z), height);
    }
  }
  return field;
}

nlohmann::json exportComparisonCase(const std::string &name,
                                    const HeightField &field, int seed,
                                    const std::filesystem::path &manifest,
                                    int repetitions) {
  const auto filename = [&](std::string_view variant) {
    return manifest.stem().string() + "." + name + "." + std::string(variant) +
           ".f32";
  };
  const auto baselineFile = filename("baseline");
  writeHeights(manifest.parent_path() / baselineFile, field.heights);
  TerrainRecipe recipe;
  recipe.size = field.size;
  recipe.cellsX = field.cellsX;
  recipe.cellsZ = field.cellsZ;
  const auto drainageStart = std::chrono::steady_clock::now();
  const auto drainage = computeTerrainDrainage(field, recipe, {});
  const auto drainageMilliseconds =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - drainageStart)
          .count();
  if (!drainage) {
    throw std::runtime_error("Comparison drainage returned no field");
  }
  nlohmann::json result = {
      {"name", name},
      {"seed", seed},
      {"shape", {field.cellsZ + 1, field.cellsX + 1}},
      {"size", {field.size.x, field.size.y}},
      {"spacing", {field.size.x / field.cellsX, field.size.y / field.cellsZ}},
      {"baseline",
       {{"file", baselineFile}, {"digest", heightDigest(field.heights)}}},
      {"drainage_milliseconds", drainageMilliseconds},
      {"native", nlohmann::json::object()}};
  for (const std::string variant : {"hydraulic", "thermal", "combined"}) {
    TerrainErosionSettings settings;
    settings.seed = seed;
    settings.iterations = variant == "thermal" ? 0 : 16;
    settings.thermalStrength = variant == "hydraulic" ? 0 : 0.35F;
    // This reports the production Standard population formula, not a second
    // erosion implementation. Keep it aligned if TerrainErosion changes
    // strategy.
    const auto droplets = settings.iterations == 0
                              ? 0
                              : int(std::clamp(double(field.heights.size()) *
                                                   settings.iterations / 256,
                                               64.0, 200000.0));
    const auto totalDroplets = std::uint64_t(droplets) * settings.iterations;
    nlohmann::json runs = nlohmann::json::array();
    std::string previousDigest;
    std::optional<TerrainErosionTransport> previousTransport;
    for (int repetition = 0; repetition < repetitions; ++repetition) {
      TerrainErosionTransport transport;
      const auto start = std::chrono::steady_clock::now();
      const auto eroded = applyTerrainErosion(field, *drainage, settings,
                                              nullptr, {}, {}, &transport);
      const auto milliseconds = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
      if (!eroded) {
        throw std::runtime_error("Comparison erosion returned no field");
      }
      const auto digest = heightDigest(*eroded);
      if (!previousDigest.empty() && previousDigest != digest) {
        throw std::runtime_error("Repeated native erosion changed its digest");
      }
      if (previousTransport && *previousTransport != transport) {
        throw std::runtime_error(
            "Repeated native erosion changed its transport ledger");
      }
      previousDigest = digest;
      previousTransport = transport;
      runs.push_back({{"milliseconds", milliseconds},
                      {"digest", digest},
                      {"transport",
                       {{"eroded_sediment", transport.erodedSediment},
                        {"deposited_sediment", transport.depositedSediment},
                        {"exported_sediment", transport.exportedSediment},
                        {"suspended_sediment", transport.suspendedSediment}}}});
      if (repetition == 0) {
        writeHeights(manifest.parent_path() / filename(variant), *eroded);
      }
    }
    result["native"][variant] = {
        {"file", filename(variant)},
        {"runs", std::move(runs)},
        {"settings",
         {{"quality", "standard"},
          {"iterations", settings.iterations},
          {"thermal_strength", settings.thermalStrength},
          {"thermal_passes", settings.thermalStrength > 0 ? 6 : 0},
          {"rain_rate", settings.rainRate},
          {"sediment_capacity", settings.sedimentCapacity},
          {"deposition_rate", settings.depositionRate},
          {"erosion_rate", 0.3F},
          {"maximum_cut_fraction", 0.05F},
          {"gravity", 9.81F},
          {"evaporation", settings.evaporation},
          {"talus_slope", settings.talusSlope},
          {"droplets_per_iteration", droplets},
          {"total_droplets", totalDroplets},
          {"maximum_steps", 96}}}};
  }
  return result;
}

nlohmann::json exportComparison(const TerrainRecipe &recipe,
                                const std::filesystem::path &source,
                                const std::string &sourceNode,
                                const std::filesystem::path &manifest,
                                const std::string &sourceRevision,
                                const std::string &buildLabel,
                                bool geometryOnly, int repetitions) {
  if (!manifest.parent_path().empty()) {
    std::filesystem::create_directories(manifest.parent_path());
  }
  const auto start = std::chrono::steady_clock::now();
  const auto representative = TerrainGenerator::generate(recipe);
  const auto generationMilliseconds =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start)
          .count();
  if (!representative) {
    throw std::runtime_error("Representative baseline generation failed");
  }
#ifdef __OPTIMIZE__
  constexpr bool optimized = true;
#else
  constexpr bool optimized = false;
#endif
#ifdef NDEBUG
  constexpr bool assertionsDisabled = true;
#else
  constexpr bool assertionsDisabled = false;
#endif
  nlohmann::json report = {
      {"format_version", 1},
      {"sample_format", "little-endian-float32"},
      {"array_order", "rows-z-columns-x"},
      {"source", source.string()},
      {"source_node", sourceNode},
      {"source_revision", sourceRevision},
      {"geometry_only", geometryOnly},
      {"generator_version", terrainGeneratorVersion},
      {"build",
       {{"label", buildLabel},
        {"compiler", __VERSION__},
        {"probe_optimized", optimized},
        {"probe_assertions_disabled", assertionsDisabled},
        {"compiled_at", __DATE__ " " __TIME__}}},
      {"canonical_recipe", recipe.toJson()},
      {"baseline_generation_milliseconds", generationMilliseconds},
      {"note",
       "Algorithms and work units differ; this is not a quality winner "
       "comparison. "
       "Native drainage is timed separately from erosion. No height "
       "normalization. Native transport ledgers cover hydraulic sample-height "
       "units only; exported and suspended sediment are explicit outcomes, "
       "not a closed surface-mass assumption."},
      {"cases", nlohmann::json::array()}};
  report["cases"].push_back(exportComparisonCase(
      "flat", analyticField(false, false), recipe.seed, manifest, repetitions));
  report["cases"].push_back(exportComparisonCase(
      "spike", analyticField(true, false), recipe.seed, manifest, repetitions));
  report["cases"].push_back(
      exportComparisonCase("anisotropic", analyticField(true, true),
                           recipe.seed, manifest, repetitions));
  report["cases"].push_back(exportComparisonCase(
      "representative", *representative, recipe.seed, manifest, repetitions));
  std::ofstream output(manifest);
  output << report.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("Cannot write comparison manifest: " +
                             manifest.string());
  }
  return report;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      throw std::invalid_argument(
          "Usage: benchmark-terrain-generation SOURCE "
          "[--geometry-only] [--repeat] [--source-node NODE] "
          "[--comparison-export FILE] [--source-revision REV] [--build-label "
          "LABEL]");
    }
    bool geometryOnly = false;
    int repetitions = 1;
    std::string sourceNode;
    std::string sourceRevision = "unspecified";
    std::string buildLabel = "unspecified";
    std::filesystem::path comparisonExport;
    for (int argument = 2; argument < argc; ++argument) {
      const std::string option = argv[argument];
      if (option == "--geometry-only") {
        geometryOnly = true;
      } else if (option == "--repeat") {
        repetitions = 2;
      } else if ((option == "--source-node" ||
                  option == "--comparison-export" ||
                  option == "--source-revision" || option == "--build-label") &&
                 argument + 1 < argc) {
        const std::string value = argv[++argument];
        if (option == "--source-node") {
          sourceNode = value;
        } else if (option == "--source-revision") {
          sourceRevision = value;
        } else if (option == "--build-label") {
          buildLabel = value;
        } else {
          comparisonExport = value;
        }
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
    if (!sourceNode.empty()) {
      selectSourceNode(recipe, sourceNode);
    }
    if (!comparisonExport.empty()) {
      if (std::filesystem::absolute(comparisonExport).lexically_normal() ==
          std::filesystem::absolute(argv[1]).lexically_normal()) {
        throw std::invalid_argument(
            "Comparison export must not overwrite the source");
      }
      const auto comparison = exportComparison(
          recipe, argv[1], sourceNode, comparisonExport, sourceRevision,
          buildLabel, geometryOnly, repetitions);
      std::cout << comparison.dump(2) << '\n';
      return 0;
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
