#include "demi/runtime/terrain/RegionalTerrainSampler.h"
#include "demi/runtime/terrain/RegionalTerrainCover.h"
#include "demi/runtime/terrain/TerrainBiomeRules.h"

#include "demi/runtime/terrain/TerrainEvaluation.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"
#include "demi/runtime/terrain/TerrainGraphPointMath.h"
#include "demi/runtime/terrain/TerrainGraphSpatialMasks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>

namespace demi::runtime {
namespace {
using namespace terrain_graph_detail;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::invalid_argument(message);
  }
}

std::int64_t lastSample(std::int64_t first, std::uint32_t cells,
                        std::uint32_t stride) {
  require(
      first >= 0 && cells > 0 && stride > 0,
      "Regional tile requires nonnegative indices, positive cells and stride");
  require(std::uint64_t(cells) <=
              std::uint64_t(std::numeric_limits<std::int64_t>::max() - first) /
                  stride,
          "Regional tile lattice range overflows");
  return first + std::int64_t(cells) * stride;
}

struct RegionalSource {
  // TerrainEvaluation borrows recipe members. Heap-owned sources keep that
  // lifetime stable even when the compiled instruction vector moves.
  TerrainRecipe recipe;
  TerrainEvaluation evaluation;
  explicit RegionalSource(TerrainRecipe source)
      : recipe(std::move(source)), evaluation(recipe) {}
};

TerrainRecipe sourceRecipe(const TerrainRecipe &authored,
                           const TerrainGraphNode &node,
                           const nlohmann::json &parameters) {
  auto recipe = authored;
  recipe.graph = nullptr;
  if (node.type == "landform") {
    return recipe;
  }
  TerrainLandform shape;
  if (node.type == "constant") {
    shape.baseHeight = parameters.at("height").get<float>();
    shape.heightVariation = 0;
  } else {
    auto values = parameters;
    values.erase("seed_offset");
    shape = TerrainLandform::parse(values);
    const auto seed =
        std::int64_t(recipe.seed) + parameters.at("seed_offset").get<int>();
    require(seed >= INT32_MIN && seed <= INT32_MAX,
            "Regional noise seed offset overflows the seed range");
    recipe.seed = static_cast<int>(seed);
  }
  recipe.landforms = {{"graph", shape}};
  recipe.defaultLandform = "graph";
  for (auto &[id, biome] : recipe.biomes) {
    biome.landform = "graph";
  }
  recipe.rules.clear();
  recipe.regions.clear();
  return recipe;
}
} // namespace

struct RegionalTerrainSampler::Impl {
  enum class Kind { Source, Band, Spatial, Blend, Offset, Pass };
  struct Node {
    Kind kind = Kind::Source;
    std::size_t first = 0;
    std::size_t second = 0;
    std::optional<std::size_t> mask;
    std::unique_ptr<RegionalSource> source;
    std::optional<TerrainSpatialMask> spatial;
    double amount = 0;
    float minimum = 0;
    float maximum = 0;
    float falloff = 0;
  };

  TerrainRegionConfig settings;
  std::unique_ptr<RegionalSource> base;
  std::vector<Node> nodes;
  std::size_t output = 0;
  bool scatter = false;
  std::unique_ptr<TerrainBiomeRuleEvaluator> biomeRules;

  explicit Impl(TerrainRegionConfig config) : settings(std::move(config)) {
    const auto &value = settings;
    value.recipe.validate();
    require(std::isfinite(value.originX) && std::isfinite(value.originZ) &&
                std::isfinite(value.baseSampleSpacing) &&
                value.baseSampleSpacing > 0 &&
                std::isfinite(value.normalSampleSpacing) &&
                value.normalSampleSpacing > 0,
            "Regional origins and sample steps must be finite with positive "
            "sample steps");
    require(
        std::isfinite(value.originX + value.recipe.size.x) &&
            std::isfinite(value.originZ + value.recipe.size.y) &&
            value.originX + value.baseSampleSpacing > value.originX &&
            value.originZ + value.baseSampleSpacing > value.originZ &&
            value.originX + value.recipe.size.x + value.normalSampleSpacing >
                value.originX + value.recipe.size.x &&
            value.originZ + value.recipe.size.y + value.normalSampleSpacing >
                value.originZ + value.recipe.size.y,
        "Regional terrain lattice is not representable at these bounds");
    const double coordinateUlp = std::max(
        double(std::nextafter(value.recipe.size.x,
                              std::numeric_limits<float>::infinity())) -
            value.recipe.size.x,
        double(std::nextafter(value.recipe.size.y,
                              std::numeric_limits<float>::infinity())) -
            value.recipe.size.y);
    require(value.baseSampleSpacing >= coordinateUlp &&
                value.normalSampleSpacing >= coordinateUlp,
            "Regional sample spacing is below the source evaluator coordinate "
            "precision");
    auto baseRecipe = value.recipe;
    baseRecipe.graph = nullptr;
    base = std::make_unique<RegionalSource>(std::move(baseRecipe));
    const auto validateHalo = [&](const RegionalSource &source) {
      const Vec2 extent{
          float(double(value.recipe.size.x) + 2 * value.normalSampleSpacing),
          float(double(value.recipe.size.y) + 2 * value.normalSampleSpacing)};
      for (const auto &[id, shape] : source.recipe.landforms) {
        shape.validate(extent);
      }
    };
    validateHalo(*base);
    require(base->evaluation.edits().empty(),
            "Regional recipe: active sculpt/protection edits require "
            "coordinated surface preparation");
    for (const auto &rule : value.recipe.rules) {
      require(!rule.moisture.enabled && !rule.waterDistance.enabled &&
                  rule.substrate.empty(),
              "Regional biome rule " + rule.id +
                  ": moisture, water and substrate need regional environment "
                  "measurements");
    }
    if (!value.recipe.rules.empty()) {
      require(value.recipe.regions.empty(),
              "Regional automatic biomes with painted overlays require "
              "coordinated preparation");
      biomeRules = std::make_unique<TerrainBiomeRuleEvaluator>(settings.recipe);
    }
    if (value.recipe.graph.is_null()) {
      scatter = bool(value.palette);
      return;
    }
    require(value.recipe.regions.empty(),
            "Regional graph recipe: painted biome overlays require coordinated "
            "surface preparation");
    const auto graph = TerrainGraph::parse(value.recipe.graph);
    for (const auto &link : graph.links) {
      if (link.to.node == graph.output && link.to.port == "water") {
        throw std::invalid_argument("Node " + graph.output + ", port " +
                                    link.to.port +
                                    ": regional point sampling cannot discard "
                                    "connected water/instance output");
      }
    }
    if (const auto *instances = graph.input(graph.output, "instances")) {
      const auto node = std::ranges::find(graph.nodes, instances->from.node,
                                          &TerrainGraphNode::id);
      const auto *surface = graph.input(graph.output, "field");
      const auto *scatterSurface = graph.input(instances->from.node, "field");
      require(node != graph.nodes.end() && node->type == "scatter" && surface &&
                  scatterSurface &&
                  surface->from.node == scatterSurface->from.node &&
                  !graph.input(node->id, "water"),
              "Regional port instances requires Scatter Palette on the final "
              "output surface without a water input");
      require(value.palette != nullptr,
              "Regional port instances requires a resolved palette");
      scatter = true;
    }
    std::map<std::string, std::size_t, std::less<>> indices;
    for (const auto &id : graph.executionOrder()) {
      const auto &authored = *graph.node(id);
      const auto parameters = terrainGraphNodeParameters(authored);
      Node compiled;
      const auto input = [&](std::string_view port) {
        const auto *link = graph.input(id, port);
        if (!link) {
          throw std::invalid_argument("Node " + id + ": missing " +
                                      std::string(port));
        }
        return indices.at(link->from.node);
      };
      try {
        if (authored.type == "noise" || authored.type == "constant" ||
            authored.type == "landform") {
          compiled.source = std::make_unique<RegionalSource>(
              sourceRecipe(value.recipe, authored, parameters));
          validateHalo(*compiled.source);
        } else if (authored.type == "elevation_mask") {
          compiled.kind = Kind::Band;
          compiled.first = input("field");
          compiled.minimum = parameters.at("minimum").get<float>();
          compiled.maximum = parameters.at("maximum").get<float>();
          compiled.falloff = parameters.at("falloff").get<float>();
        } else if (authored.type == "path_distance_mask" ||
                   authored.type == "polygon_mask") {
          compiled.kind = Kind::Spatial;
          compiled.first = input("field");
          compiled.spatial = parseTerrainSpatialMask(
              parameters, authored.type == "polygon_mask");
        } else if (authored.type == "height_blend" ||
                   authored.type == "offset") {
          const bool blend = authored.type == "height_blend";
          compiled.kind = blend ? Kind::Blend : Kind::Offset;
          compiled.first = input(blend ? "a" : "field");
          if (blend) {
            compiled.second = input("b");
          }
          if (graph.input(id, "mask")) {
            compiled.mask = input("mask");
          }
          compiled.amount = parameters.at("amount").get<double>();
        } else if (authored.type == "output" || authored.type == "biomes" ||
                   authored.type == "scatter") {
          compiled.kind = Kind::Pass;
          compiled.first = input("field");
        } else {
          throw std::invalid_argument("unsupported regional point node type '" +
                                      authored.type +
                                      "'; use coordinated regional preparation "
                                      "instead of independent tile simulation");
        }
      } catch (const std::exception &error) {
        throw std::invalid_argument("Node " + id + ": " + error.what());
      }
      indices.emplace(id, nodes.size());
      nodes.push_back(std::move(compiled));
    }
    output = indices.at(graph.output);
    if (biomeRules) {
      const auto *link = graph.input(graph.output, "field");
      require(link && std::ranges::any_of(graph.nodes,
                                          [&](const auto &node) {
                                            return node.id == link->from.node &&
                                                   node.type == "biomes";
                                          }),
              "Regional biome rules require a final Biomes node before Output");
      for (const auto &node : graph.nodes) {
        if (node.type == "biomes") {
          require(node.id == link->from.node,
                  "Regional biome assignment currently requires a single final "
                  "Biomes node");
        }
      }
    }
  }

  std::size_t biome(Vec2 local, float height, Vec3 normal) const {
    if (!biomeRules) {
      return base->evaluation.baseSample(local).biome;
    }
    const float slope = std::acos(std::clamp(normal.y, -1.F, 1.F)) * 180.F /
                        3.14159265358979323846F;
    return biomeRules->evaluate({.height = height, .slope = slope}).biome;
  }

  void checkPoint(double x, double z, double halo) const {
    require(std::isfinite(x) && std::isfinite(z) &&
                x >= settings.originX - halo &&
                x <= settings.originX + settings.recipe.size.x + halo &&
                z >= settings.originZ - halo &&
                z <= settings.originZ + settings.recipe.size.y + halo,
            "Regional terrain sample lies outside its bounds and normal halo");
  }

  float height(double globalX, double globalZ, std::span<float> scratch) const {
    const double x = globalX - settings.originX;
    const double z = globalZ - settings.originZ;
    // Existing finite sources use Vec2 floats. Preserve that canonical
    // evaluator (and its seed/profile/warp math) while world ownership stays
    // double/lattice based.
    const Vec2 local{float(x), float(z)};
    if (nodes.empty()) {
      return base->evaluation.baseSample(local).height;
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      const auto &node = nodes[index];
      switch (node.kind) {
      case Kind::Source:
        scratch[index] = node.source->evaluation.baseSample(local).height;
        break;
      case Kind::Band:
        scratch[index] = terrainGraphBandWeight(
            scratch[node.first], node.minimum, node.maximum, node.falloff);
        break;
      case Kind::Spatial:
        scratch[index] =
            *sampleTerrainSpatialMask(*node.spatial, local.x, local.y);
        break;
      case Kind::Blend:
      case Kind::Offset:
        scratch[index] = terrainGraphModifiedHeight(
            scratch[node.first],
            node.kind == Kind::Blend ? scratch[node.second] : 0, node.amount,
            node.mask ? scratch[*node.mask] : 1, node.kind == Kind::Blend);
        break;
      case Kind::Pass:
        scratch[index] = scratch[node.first];
        break;
      }
    }
    return scratch[output];
  }

  Vec3 normal(double x, double z, std::span<float> scratch) const {
    const double step = settings.normalSampleSpacing;
    const double dx =
        (double(height(x + step, z, scratch)) - height(x - step, z, scratch)) /
        (2 * step);
    const double dz =
        (double(height(x, z + step, scratch)) - height(x, z - step, scratch)) /
        (2 * step);
    const double length = std::hypot(dx, 1.0, dz);
    return {float(-dx / length), float(1 / length), float(-dz / length)};
  }
};

RegionalTerrainSampler::RegionalTerrainSampler(TerrainRegionConfig config)
    : impl_(std::make_shared<const Impl>(config)) {}

const TerrainRegionConfig &RegionalTerrainSampler::config() const {
  return impl_->settings;
}

bool RegionalTerrainSampler::hasScatter() const { return impl_->scatter; }

std::pair<float, float> RegionalTerrainSampler::patchHeightBounds(
    TerrainRegionPatchKey key, std::stop_token stop) const {
  const auto &settings = config();
  const auto cells = settings.recipe.chunkCells;
  require(cells > 0 && (cells & (cells - 1)) == 0,
          "Regional patch chunk_cells must be a power of two");
  (void)regionPatchRectangle(settings, key);
  const double denominator = std::ldexp(double(cells), int(key.level));
  const double stepX = settings.recipe.size.x / denominator;
  const double stepZ = settings.recipe.size.y / denominator;
  std::vector<float> scratch(impl_->nodes.size());
  std::pair<float, float> range{std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::lowest()};
  for (int z = 0; z <= cells; ++z) {
    if (stop.stop_requested()) {
      throw std::runtime_error("Regional height-bound preparation cancelled");
    }
    for (int x = 0; x <= cells; ++x) {
      const double globalX = settings.originX + (double(key.x) * cells + x) * stepX;
      const double globalZ = settings.originZ + (double(key.z) * cells + z) * stepZ;
      const auto height = impl_->height(globalX, globalZ, scratch);
      range.first = std::min(range.first, height);
      range.second = std::max(range.second, height);
    }
  }
  return range;
}

float RegionalTerrainSampler::sampleHeight(double x, double z) const {
  impl_->checkPoint(x, z, config().normalSampleSpacing);
  std::array<float, 128> local{};
  std::vector<float> extended;
  if (impl_->nodes.size() > local.size()) {
    extended.resize(impl_->nodes.size());
  }
  return impl_->height(x, z,
                       extended.empty() ? std::span<float>(local)
                                        : std::span<float>(extended));
}

Vec3 RegionalTerrainSampler::sampleNormal(double x, double z) const {
  impl_->checkPoint(x, z, 0);
  std::array<float, 128> local{};
  std::vector<float> extended;
  if (impl_->nodes.size() > local.size()) {
    extended.resize(impl_->nodes.size());
  }
  return impl_->normal(x, z,
                       extended.empty() ? std::span<float>(local)
                                        : std::span<float>(extended));
}

TerrainRegionTile
RegionalTerrainSampler::sampleTile(TerrainRegionTileRequest request,
                                   std::stop_token stop) const {
  const auto lastX =
      lastSample(request.firstSampleX, request.cellsX, request.stride);
  const auto lastZ =
      lastSample(request.firstSampleZ, request.cellsZ, request.stride);
  const auto &settings = config();
  require(lastX <= (std::int64_t{1} << 53) && lastZ <= (std::int64_t{1} << 53),
          "Regional tile indices exceed exact double lattice representation");
  require(
      double(lastX) * settings.baseSampleSpacing <= settings.recipe.size.x &&
          double(lastZ) * settings.baseSampleSpacing <= settings.recipe.size.y,
      "Regional tile extends outside the authored world bounds");
  const std::uint64_t columns = std::uint64_t(request.cellsX) + 1;
  const std::uint64_t rows = std::uint64_t(request.cellsZ) + 1;
  require(columns <= std::numeric_limits<std::size_t>::max() / rows &&
              request.cellsX <=
                  std::uint32_t(std::numeric_limits<int>::max()) &&
              request.cellsZ <= std::uint32_t(std::numeric_limits<int>::max()),
          "Regional tile sample grid exceeds host representation");
  TerrainRegionTile tile;
  tile.originX = settings.originX +
                 double(request.firstSampleX) * settings.baseSampleSpacing;
  tile.originZ = settings.originZ +
                 double(request.firstSampleZ) * settings.baseSampleSpacing;
  tile.sampleSpacing = double(request.stride) * settings.baseSampleSpacing;
  require(std::isfinite(float(tile.sampleSpacing)) &&
              float(tile.sampleSpacing) > 0,
          "Regional tile spacing is not representable in a local heightfield");
  auto &field = tile.field;
  field.projectionOrigin = {float(tile.originX - settings.originX),
                            float(tile.originZ - settings.originZ)};
  field.paletteId = settings.recipe.paletteId;
  field.size = {float(double(request.cellsX) * tile.sampleSpacing),
                float(double(request.cellsZ) * tile.sampleSpacing)};
  require(std::isfinite(field.size.x) && std::isfinite(field.size.y) &&
              field.size.x > 0 && field.size.y > 0,
          "Regional tile extent is not representable in a local heightfield");
  field.cellsX = static_cast<int>(request.cellsX);
  field.cellsZ = static_cast<int>(request.cellsZ);
  const auto count = static_cast<std::size_t>(columns * rows);
  field.heights.resize(count);
  field.baseHeights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  field.exclusions.resize(count);
  for (const auto &[id, biome] : settings.recipe.biomes) {
    field.biomeIds.push_back(id);
    field.biomeColors.push_back(biome.color);
    field.biomeMaterials.push_back(biome.material);
    field.biomeTextureScales.push_back(biome.textureScale);
  }
  std::vector<float> scratch(impl_->nodes.size());
  for (std::uint32_t z = 0; z <= request.cellsZ; ++z) {
    if (stop.stop_requested()) {
      throw std::runtime_error("Regional tile preparation cancelled");
    }
    const double globalZ =
        settings.originZ +
        double(request.firstSampleZ + std::int64_t(z) * request.stride) *
            settings.baseSampleSpacing;
    for (std::uint32_t x = 0; x <= request.cellsX; ++x) {
      const double globalX =
          settings.originX +
          double(request.firstSampleX + std::int64_t(x) * request.stride) *
              settings.baseSampleSpacing;
      const auto index = field.index(static_cast<int>(x), static_cast<int>(z));
      const float height = impl_->height(globalX, globalZ, scratch);
      field.heights.set(index, height);
      field.baseHeights.set(index, height);
      field.normals.set(index, impl_->normal(globalX, globalZ, scratch));
      const Vec2 local{float(globalX - settings.originX),
                       float(globalZ - settings.originZ)};
      field.biomeIndices.set(
          index, impl_->biome(local, height, field.normals.at(index)));
      field.exclusions.set(index, impl_->base->evaluation.exclusion(local));
    }
  }
  for (int z = 0; z < field.cellsZ;) {
    const int depth = std::min(settings.recipe.chunkCells, field.cellsZ - z);
    for (int x = 0; x < field.cellsX;) {
      const int width = std::min(settings.recipe.chunkCells, field.cellsX - x);
      field.chunks.push_back({x, z, width, depth});
      x += width;
    }
    z += depth;
  }
  return tile;
}
TerrainRegionTile
RegionalTerrainSampler::samplePatch(TerrainRegionPatchKey key,
                                    std::uint8_t stitch,
                                    std::stop_token stop) const {
  const auto &settings = config();
  const auto cells = settings.recipe.chunkCells;
  require(cells > 0 && (cells & (cells - 1)) == 0,
          "Regional patch chunk_cells must be a power of two");
  require(stitch < 16, "Invalid regional stitch mask");
  const auto box = regionPatchRectangle(settings, key);
  TerrainRegionTile tile;
  tile.originX = box.x;
  tile.originZ = box.z;
  tile.sampleSpacing = std::max(box.width, box.depth) / cells;
  auto &field = tile.field;
  field.projectionOrigin = {float(tile.originX - settings.originX),
                            float(tile.originZ - settings.originZ)};
  field.size = {float(box.width), float(box.depth)};
  field.cellsX = field.cellsZ = cells;
  const auto count = (std::size_t(cells) + 1) * (std::size_t(cells) + 1);
  field.heights.resize(count);
  field.baseHeights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  field.exclusions.resize(count);
  field.paletteId = settings.recipe.paletteId;
  for (const auto &[id, biome] : settings.recipe.biomes) {
    field.biomeIds.push_back(id);
    field.biomeColors.push_back(biome.color);
    field.biomeMaterials.push_back(biome.material);
    field.biomeTextureScales.push_back(biome.textureScale);
  }
  const double denominator = std::ldexp(double(cells), int(key.level));
  const double stepX = settings.recipe.size.x / denominator;
  const double stepZ = settings.recipe.size.y / denominator;
  std::vector<float> scratch(impl_->nodes.size());
  for (int z = 0; z <= cells; ++z) {
    if (stop.stop_requested()) {
      throw std::runtime_error("Regional patch preparation cancelled");
    }
    for (int x = 0; x <= cells; ++x) {
      const double globalX =
          settings.originX + (double(key.x) * cells + x) * stepX;
      const double globalZ =
          settings.originZ + (double(key.z) * cells + z) * stepZ;
      const auto index = field.index(x, z);
      const auto height = impl_->height(globalX, globalZ, scratch);
      field.heights.set(index, height);
      field.baseHeights.set(index, height);
      field.normals.set(index, impl_->normal(globalX, globalZ, scratch));
      const Vec2 local{float(globalX - settings.originX),
                       float(globalZ - settings.originZ)};
      field.biomeIndices.set(
          index, impl_->biome(local, height, field.normals.at(index)));
      field.exclusions.set(index, impl_->base->evaluation.exclusion(local));
    }
  }
  const auto stitchPoint = [&](int x, int z, int dx, int dz) {
    const auto index = field.index(x, z);
    const auto a = field.index(x - dx, z - dz), b = field.index(x + dx, z + dz);
    field.heights.set(index, (field.heights.at(a) + field.heights.at(b)) * .5F);
    const auto na = field.normals.at(a), nb = field.normals.at(b);
    field.normals.set(
        index, {(na.x + nb.x) * .5F, (na.y + nb.y) * .5F, (na.z + nb.z) * .5F});
  };
  for (int i = 1; i < cells; i += 2) {
    if (stitch & 1) {
      stitchPoint(0, i, 0, 1);
    }
    if (stitch & 2) {
      stitchPoint(cells, i, 0, 1);
    }
    if (stitch & 4) {
      stitchPoint(i, 0, 1, 0);
    }
    if (stitch & 8) {
      stitchPoint(i, cells, 1, 0);
    }
  }
  field.chunks.push_back({0, 0, cells, cells});
  return tile;
}
} // namespace demi::runtime
