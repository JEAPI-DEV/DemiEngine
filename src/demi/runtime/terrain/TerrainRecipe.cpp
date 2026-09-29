#include "demi/runtime/terrain/TerrainRecipe.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::invalid_argument(message);
}
bool finite(float value) { return std::isfinite(value); }
Vec2 vector(const nlohmann::json &json) {
  require(json.is_array() && json.size() == 2,
          "Terrain vector must contain [x,z]");
  return {json.at(0).get<float>(), json.at(1).get<float>()};
}
int integer(const nlohmann::json &json) {
  require(json.is_number_integer(), "Terrain integer field must be an integer");
  const auto number = json.get<long double>();
  require(number >= std::numeric_limits<int>::min() &&
              number <= std::numeric_limits<int>::max(),
          "Terrain integer is out of range");
  return static_cast<int>(number);
}
void brush(Vec2 center, float radius, float strength, float falloff) {
  require(finite(center.x) && finite(center.y) && finite(radius) &&
              radius > 0 && finite(strength) && strength >= 0 &&
              strength <= 1 && finite(falloff) && falloff >= 0,
          "Terrain brush requires finite center, positive radius, strength in "
          "[0,1], nonnegative falloff");
}
TerrainEditKind editKind(const std::string &name) {
  if (name == "raise")
    return TerrainEditKind::Raise;
  if (name == "lower")
    return TerrainEditKind::Lower;
  if (name == "flatten")
    return TerrainEditKind::Flatten;
  if (name == "smooth")
    return TerrainEditKind::Smooth;
  if (name == "protect")
    return TerrainEditKind::Protect;
  throw std::invalid_argument("Unknown terrain edit type");
}
const char *editName(TerrainEditKind kind) {
  switch (kind) {
  case TerrainEditKind::Raise:
    return "raise";
  case TerrainEditKind::Lower:
    return "lower";
  case TerrainEditKind::Flatten:
    return "flatten";
  case TerrainEditKind::Smooth:
    return "smooth";
  case TerrainEditKind::Protect:
    return "protect";
  }
  throw std::invalid_argument("Unknown terrain edit kind");
}
TerrainLayerKind layerKind(const std::string &name) {
  if (name == "generation")
    return TerrainLayerKind::Generation;
  if (name == "biome")
    return TerrainLayerKind::Biome;
  if (name == "sculpt")
    return TerrainLayerKind::Sculpt;
  if (name == "protection")
    return TerrainLayerKind::Protection;
  if (name == "exclusion")
    return TerrainLayerKind::Exclusion;
  throw std::invalid_argument("Unknown terrain layer type");
}
const char *layerName(TerrainLayerKind kind) {
  switch (kind) {
  case TerrainLayerKind::Generation:
    return "generation";
  case TerrainLayerKind::Biome:
    return "biome";
  case TerrainLayerKind::Sculpt:
    return "sculpt";
  case TerrainLayerKind::Protection:
    return "protection";
  case TerrainLayerKind::Exclusion:
    return "exclusion";
  }
  throw std::invalid_argument("Unknown terrain layer kind");
}
void checkKeys(const nlohmann::json &json,
               std::initializer_list<std::string_view> allowed) {
  require(json.is_object(), "Terrain document entry must be an object");
  for (auto entry = json.begin(); entry != json.end(); ++entry) {
    if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end())
      throw std::invalid_argument("Unknown terrain field: " + entry.key());
  }
}
template <class T> void readBrush(const nlohmann::json &json, T &value) {
  require(json.is_object(), "Terrain brush must be an object");
  if (json.contains("center"))
    value.center = vector(json.at("center"));
  value.radius = json.value("radius", value.radius);
  value.strength = json.value("strength", value.strength);
  value.falloff = json.value("falloff", value.falloff);
}
} // namespace

float terrainBrushWeight(Vec2 position, Vec2 center, float radius,
                         float strength, float falloff) {
  const double distance =
      std::hypot(double(position.x) - center.x, double(position.y) - center.y);
  if (distance > radius)
    return 0;
  if (falloff == 0)
    return strength;
  return strength * static_cast<float>(std::pow(
                        std::max(0.0, 1 - distance / radius), falloff));
}

std::size_t TerrainRecipe::sampleCount() const {
  require(cellsX > 0 && cellsZ > 0,
          "Terrain resolution must contain positive cell counts");
  const auto width = std::size_t(cellsX) + 1;
  const auto depth = std::size_t(cellsZ) + 1;
  require(width <= std::numeric_limits<std::size_t>::max() / depth,
          "Terrain sample count overflows");
  const auto count = width * depth;
  require(count <= std::vector<Vec3>{}.max_size() &&
              count <= std::vector<std::size_t>{}.max_size(),
          "Terrain sample allocation exceeds container capacity");
  // Peak dense storage includes base, edits, smooth snapshot, exclusions,
  // normals, indices, and protection state. Check arithmetic rather than
  // imposing a content cap.
  constexpr std::size_t bytes = 4 * sizeof(float) + sizeof(Vec3) +
                                sizeof(std::size_t) + sizeof(unsigned char);
  require(count <= std::numeric_limits<std::size_t>::max() / bytes,
          "Terrain allocation byte count overflows");
  return count;
}
void TerrainRecipe::validate() const {
  (void)sampleCount();
  require(finite(size.x) && finite(size.y) && size.x > 0 && size.y > 0 &&
              size.x / cellsX > 0 && size.y / cellsZ > 0,
          "Terrain size and sample spacing must be positive and finite");
  // Vertices use float positions. The largest adjacent representable gap on
  // [0,size] is the gap immediately below size; smaller spacing can collapse
  // neighboring samples into the same position and create degenerate cells.
  const double spacingX = double(size.x) / cellsX;
  const double spacingZ = double(size.y) / cellsZ;
  const double endpointGapX = double(size.x) - std::nextafter(size.x, 0.F);
  const double endpointGapZ = double(size.y) - std::nextafter(size.y, 0.F);
  require(spacingX >= endpointGapX && spacingZ >= endpointGapZ,
          "Terrain sample spacing must resolve adjacent floating-point "
          "positions at its size endpoint");
  require(chunkCells > 0, "Terrain chunk_cells must be positive");
  const auto chunksX = (std::size_t(cellsX) - 1) / std::size_t(chunkCells) + 1;
  const auto chunksZ = (std::size_t(cellsZ) - 1) / std::size_t(chunkCells) + 1;
  require(chunksX <= std::numeric_limits<std::size_t>::max() / chunksZ,
          "Terrain chunk count overflows");
  constexpr std::size_t chunkBytes = 4 * sizeof(int);
  require(chunksX * chunksZ <=
              std::size_t(std::numeric_limits<std::ptrdiff_t>::max()) /
                  chunkBytes,
          "Terrain chunk allocation exceeds container capacity");
  std::map<std::string, TerrainLayerKind> layerKinds;
  std::size_t generationLayers = 0;
  for (const auto &layer : layers) {
    require(!layer.id.empty(), "Terrain layer id must not be empty");
    (void)layerName(layer.kind);
    require(layerKinds.emplace(layer.id, layer.kind).second,
            "Duplicate terrain layer id");
    if (layer.kind == TerrainLayerKind::Generation)
      ++generationLayers;
  }
  require(generationLayers == 1,
          "Terrain requires exactly one generation layer");
  auto requireLayer = [&](const std::string &id, TerrainLayerKind kind) {
    const auto layer = layerKinds.find(id);
    require(layer != layerKinds.end(),
            "Terrain stroke references an unknown layer");
    require(layer->second == kind,
            "Terrain stroke references an incompatible layer kind");
  };
  require(!biomes.empty() && biomes.contains(defaultBiome),
          "Terrain default_biome must reference an existing biome");
  for (const auto &[id, biome] : biomes) {
    require(!id.empty(), "Terrain biome id must not be empty");
    require(finite(biome.baseHeight) && finite(biome.heightVariation) &&
                biome.heightVariation >= 0 && finite(biome.featureSize) &&
                biome.featureSize > 0 && finite(biome.roughness) &&
                biome.roughness >= 0 && biome.roughness <= 1 &&
                biome.octaves > 0,
            "Invalid terrain biome height/noise parameters");
    require(double(std::abs(biome.baseHeight)) + biome.heightVariation <=
                std::numeric_limits<float>::max(),
            "Terrain biome height range overflows");
    require(std::isfinite(1.F / biome.featureSize),
            "Terrain feature_size produces an unrepresentable noise frequency");
    // FastNoiseLite floors lattice positions into signed ints; its octave
    // frequencies double. Reject inputs outside that representable domain.
    const double frequency =
        std::ldexp(1.0 / biome.featureSize, biome.octaves - 1);
    require(std::isfinite(frequency) &&
                frequency * std::max(size.x, size.y) <
                    std::numeric_limits<int>::max() / 4.0,
            "Terrain octave coordinates exceed noise lattice range");
    for (float channel :
         {biome.color.r, biome.color.g, biome.color.b, biome.color.a})
      require(finite(channel) && channel >= 0 && channel <= 1,
              "Terrain colors must be RGBA values in [0,1]");
  }
  for (const auto &region : regions) {
    brush(region.center, region.radius, region.strength, region.falloff);
    require(biomes.contains(region.biome),
            "Terrain region references an unknown biome");
    requireLayer(region.layer, TerrainLayerKind::Biome);
  }
  for (const auto &edit : edits) {
    brush(edit.center, edit.radius, edit.strength, edit.falloff);
    (void)editName(edit.kind);
    const bool protection = edit.kind == TerrainEditKind::Protect;
    const std::string layer = edit.layer.empty()
                                  ? (protection ? "protection" : "sculpt")
                                  : edit.layer;
    requireLayer(layer, protection ? TerrainLayerKind::Protection
                                   : TerrainLayerKind::Sculpt);
    require(finite(edit.amount) && edit.amount >= 0 &&
                finite(edit.targetHeight),
            "Invalid terrain edit amount/target_height");
    if (edit.kind != TerrainEditKind::Protect) {
      require(edit.samples.empty(),
              "Only protection edits may contain snapshots");
      continue;
    }
    require(edit.snapshotCellsX == cellsX && edit.snapshotCellsZ == cellsZ &&
                edit.snapshotSize.x == size.x && edit.snapshotSize.y == size.y,
            "Terrain protection snapshot grid changed; explicitly recapture "
            "protection");
    std::set<std::pair<int, int>> seen;
    for (const auto &sample : edit.samples) {
      require(finite(sample.position.x) && finite(sample.position.y) &&
                  finite(sample.height) && sample.position.x >= 0 &&
                  sample.position.x <= size.x && sample.position.y >= 0 &&
                  sample.position.y <= size.y,
              "Invalid terrain protection sample");
      const auto x = std::llround(double(sample.position.x) / size.x * cellsX);
      const auto z = std::llround(double(sample.position.y) / size.y * cellsZ);
      const float gridX = float(double(x) * size.x / cellsX);
      const float gridZ = float(double(z) * size.y / cellsZ);
      require(sample.position.x == gridX && sample.position.y == gridZ &&
                  terrainBrushWeight(sample.position, edit.center, edit.radius,
                                     edit.strength, edit.falloff) > 0,
              "Terrain protection samples must lie on the captured grid inside "
              "the brush");
      require(seen.emplace(int(x), int(z)).second,
              "Duplicate terrain protection sample");
    }
  }
  for (const auto &exclusion : exclusions) {
    brush(exclusion.center, exclusion.radius, exclusion.strength,
          exclusion.falloff);
    require(finite(exclusion.value) && exclusion.value >= 0 &&
                exclusion.value <= 1,
            "Terrain exclusion value must be in [0,1]");
    requireLayer(exclusion.layer, TerrainLayerKind::Exclusion);
  }
}

TerrainRecipe TerrainRecipe::parse(const nlohmann::json &json) {
  try {
    require(json.is_object(), "Terrain recipe must be an object");
    checkKeys(json, {"format_version", "size", "resolution", "seed",
                     "chunk_cells", "default_biome", "biomes", "layers",
                     "regions", "edits", "exclusions"});
    require(json.contains("format_version") &&
                integer(json.at("format_version")) == formatVersion,
            "Unsupported terrain recipe format_version");
    TerrainRecipe recipe;
    if (json.contains("size"))
      recipe.size = vector(json.at("size"));
    if (json.contains("resolution")) {
      const auto &entry = json.at("resolution");
      require(entry.is_array() && entry.size() == 2,
              "Terrain resolution must be [cellsX,cellsZ]");
      recipe.cellsX = integer(entry.at(0));
      recipe.cellsZ = integer(entry.at(1));
    }
    if (json.contains("seed"))
      recipe.seed = integer(json.at("seed"));
    if (json.contains("chunk_cells"))
      recipe.chunkCells = integer(json.at("chunk_cells"));
    recipe.defaultBiome = json.value("default_biome", recipe.defaultBiome);
    if (json.contains("layers")) {
      require(json.at("layers").is_array(), "Terrain layers must be an array");
      recipe.layers.clear();
      for (const auto &entry : json.at("layers")) {
        checkKeys(entry, {"id", "name", "kind", "enabled"});
        TerrainLayer layer;
        layer.id = entry.at("id").get<std::string>();
        layer.name = entry.value("name", layer.id);
        layer.kind = layerKind(entry.value("kind", "sculpt"));
        layer.enabled = entry.value("enabled", layer.enabled);
        recipe.layers.push_back(std::move(layer));
      }
    }
    if (json.contains("biomes")) {
      require(json.at("biomes").is_object(),
              "Terrain biomes must be an object keyed by stable ids");
      recipe.biomes.clear();
      for (auto biomeEntry = json.at("biomes").begin();
           biomeEntry != json.at("biomes").end(); ++biomeEntry) {
        const auto &entry = biomeEntry.value();
        require(entry.is_object(), "Terrain biome must be an object");
        checkKeys(entry, {"base_height", "height_variation", "feature_size",
                          "roughness", "octaves", "color"});
        TerrainBiome biome;
        biome.baseHeight = entry.value("base_height", biome.baseHeight);
        biome.heightVariation =
            entry.value("height_variation", biome.heightVariation);
        biome.featureSize = entry.value("feature_size", biome.featureSize);
        biome.roughness = entry.value("roughness", biome.roughness);
        if (entry.contains("octaves"))
          biome.octaves = integer(entry.at("octaves"));
        if (entry.contains("color")) {
          const auto &channels = entry.at("color");
          require(channels.is_array() && channels.size() == 4,
                  "Terrain color requires four RGBA channels");
          biome.color = {channels[0].get<float>(), channels[1].get<float>(),
                         channels[2].get<float>(), channels[3].get<float>()};
        }
        recipe.biomes.emplace(biomeEntry.key(), biome);
      }
    }
    if (json.contains("regions")) {
      require(json.at("regions").is_array(),
              "Terrain regions must be an array");
      for (const auto &entry : json.at("regions")) {
        checkKeys(entry, {"biome", "center", "radius", "strength", "falloff",
                          "layer"});
        TerrainRegion region;
        readBrush(entry, region);
        region.biome = entry.value("biome", region.biome);
        region.layer = entry.value("layer", region.layer);
        recipe.regions.push_back(region);
      }
    }
    if (json.contains("edits")) {
      require(json.at("edits").is_array(), "Terrain edits must be an array");
      for (const auto &entry : json.at("edits")) {
        TerrainEdit edit;
        readBrush(entry, edit);
        edit.kind = editKind(entry.at("type").get<std::string>());
        switch (edit.kind) {
        case TerrainEditKind::Raise:
        case TerrainEditKind::Lower:
          checkKeys(entry, {"type", "center", "radius", "strength", "falloff",
                            "amount", "layer"});
          break;
        case TerrainEditKind::Flatten:
          checkKeys(entry, {"type", "center", "radius", "strength", "falloff",
                            "target_height", "layer"});
          break;
        case TerrainEditKind::Smooth:
          checkKeys(entry, {"type", "center", "radius", "strength", "falloff",
                            "layer"});
          break;
        case TerrainEditKind::Protect:
          checkKeys(entry, {"type", "center", "radius", "strength", "falloff",
                            "snapshot", "layer"});
          break;
        }
        edit.amount = entry.value("amount", edit.amount);
        edit.targetHeight = entry.value("target_height", edit.targetHeight);
        edit.layer = entry.value("layer", edit.layer);
        if (edit.kind == TerrainEditKind::Protect) {
          const auto &snapshot = entry.at("snapshot");
          edit.snapshotSize = vector(snapshot.at("size"));
          checkKeys(snapshot, {"size", "resolution", "samples"});
          const auto &resolution = snapshot.at("resolution");
          require(resolution.is_array() && resolution.size() == 2,
                  "Protection resolution must be [cellsX,cellsZ]");
          edit.snapshotCellsX = integer(resolution[0]);
          edit.snapshotCellsZ = integer(resolution[1]);
          require(snapshot.at("samples").is_array(),
                  "Protection samples must be an array");
          for (const auto &sample : snapshot.at("samples")) {
            checkKeys(sample, {"position", "height"});
            edit.samples.push_back({vector(sample.at("position")),
                                    sample.at("height").get<float>()});
          }
        }
        recipe.edits.push_back(std::move(edit));
      }
    }
    if (json.contains("exclusions")) {
      require(json.at("exclusions").is_array(),
              "Terrain exclusions must be an array");
      for (const auto &entry : json.at("exclusions")) {
        checkKeys(entry, {"center", "radius", "strength", "falloff", "value",
                          "layer"});
        TerrainExclusion exclusion;
        readBrush(entry, exclusion);
        exclusion.value = entry.value("value", exclusion.value);
        exclusion.layer = entry.value("layer", exclusion.layer);
        recipe.exclusions.push_back(std::move(exclusion));
      }
    }
    recipe.validate();
    return recipe;
  } catch (const nlohmann::json::exception &error) {
    throw std::invalid_argument(std::string("Invalid terrain recipe: ") +
                                error.what());
  }
}

nlohmann::json TerrainRecipe::defaults() { return TerrainRecipe{}.toJson(); }
nlohmann::json TerrainRecipe::toJson() const {
  validate();
  nlohmann::json json{{"format_version", formatVersion},
                      {"size", {size.x, size.y}},
                      {"resolution", {cellsX, cellsZ}},
                      {"seed", seed},
                      {"chunk_cells", chunkCells},
                      {"default_biome", defaultBiome},
                      {"biomes", nlohmann::json::object()},
                      {"layers", nlohmann::json::array()},
                      {"regions", nlohmann::json::array()},
                      {"edits", nlohmann::json::array()},
                      {"exclusions", nlohmann::json::array()}};
  for (const auto &layer : layers) {
    nlohmann::json entry{{"id", layer.id}, {"kind", layerName(layer.kind)}};
    if (layer.name != layer.id)
      entry["name"] = layer.name;
    if (!layer.enabled)
      entry["enabled"] = false;
    json["layers"].push_back(std::move(entry));
  }
  for (const auto &[id, biome] : biomes)
    json["biomes"][id] = {
        {"base_height", biome.baseHeight},
        {"height_variation", biome.heightVariation},
        {"feature_size", biome.featureSize},
        {"roughness", biome.roughness},
        {"octaves", biome.octaves},
        {"color",
         {biome.color.r, biome.color.g, biome.color.b, biome.color.a}}};
  for (const auto &region : regions) {
    nlohmann::json entry{{"biome", region.biome},
                         {"center", {region.center.x, region.center.y}},
                         {"radius", region.radius},
                         {"strength", region.strength},
                         {"falloff", region.falloff}};
    if (region.layer != "biomes")
      entry["layer"] = region.layer;
    json["regions"].push_back(std::move(entry));
  }
  for (const auto &edit : edits) {
    nlohmann::json entry{{"type", editName(edit.kind)},
                         {"center", {edit.center.x, edit.center.y}},
                         {"radius", edit.radius},
                         {"strength", edit.strength},
                         {"falloff", edit.falloff}};
    if (!edit.layer.empty())
      entry["layer"] = edit.layer;
    if (edit.kind == TerrainEditKind::Raise ||
        edit.kind == TerrainEditKind::Lower)
      entry["amount"] = edit.amount;
    if (edit.kind == TerrainEditKind::Flatten)
      entry["target_height"] = edit.targetHeight;
    if (edit.kind == TerrainEditKind::Protect) {
      entry["snapshot"] = {
          {"size", {edit.snapshotSize.x, edit.snapshotSize.y}},
          {"resolution", {edit.snapshotCellsX, edit.snapshotCellsZ}},
          {"samples", nlohmann::json::array()}};
      for (const auto &sample : edit.samples)
        entry["snapshot"]["samples"].push_back(
            {{"position", {sample.position.x, sample.position.y}},
             {"height", sample.height}});
    }
    json["edits"].push_back(std::move(entry));
  }
  for (const auto &exclusion : exclusions) {
    nlohmann::json entry{{"center", {exclusion.center.x, exclusion.center.y}},
                         {"radius", exclusion.radius},
                         {"strength", exclusion.strength},
                         {"falloff", exclusion.falloff}};
    if (exclusion.value != 1)
      entry["value"] = exclusion.value;
    if (exclusion.layer != "exclusions")
      entry["layer"] = exclusion.layer;
    json["exclusions"].push_back(std::move(entry));
  }
  return json;
}
} // namespace demi::runtime
