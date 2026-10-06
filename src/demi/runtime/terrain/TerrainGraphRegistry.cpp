#include "demi/runtime/terrain/TerrainGraphRegistry.h"

namespace demi::runtime {
namespace {
using Kind = TerrainGraphValueKind;
using Parameter = TerrainGraphParameterKind;
TerrainGraphPort field(std::string name, bool required = true) {
  const std::string label = name == "a"   ? "First terrain"
                            : name == "b" ? "Second terrain"
                                          : "Terrain surface";
  return {std::move(name), label, Kind::Field, required};
}
TerrainGraphParameter number(std::string name, std::string label, double value,
                             std::string help) {
  return {std::move(name),
          std::move(label),
          std::move(help),
          Parameter::Number,
          value,
          {},
          {},
          {},
          false};
}
TerrainGraphParameter integer(std::string name, std::string label, int value,
                              std::string help) {
  return {std::move(name),
          std::move(label),
          std::move(help),
          Parameter::Integer,
          value,
          {},
          {},
          {},
          false};
}
std::vector<TerrainGraphNodeDefinition> makeDefinitions() {
  std::vector<TerrainGraphNodeDefinition> definitions{
      {"landform",
       "Landform",
       "Generators",
       "Generate terrain from the selected landscape recipe.",
       {},
       {field("field")},
       {}},
      {"noise",
       "Noise terrain",
       "Generators",
       "Generate a seeded heightfield. Connect several sources to a Height "
       "Blend.",
       {},
       {field("field")},
       {number("base_height", "Base height", 0, "Elevation in world units."),
        number("height_variation", "Height variation", 8,
               "Noise amplitude in world units; nonnegative."),
        number("feature_size", "Feature size", 32,
               "Width of terrain features in world units; positive."),
        number("roughness", "Roughness", .5,
               "Contribution of smaller noise scales, from zero to one."),
        integer("octaves", "Octaves", 4, "Number of noise scales."),
        integer("seed_offset", "Seed offset", 0,
                "Offset from the world seed for this source.")}},
      {"constant",
       "Flat terrain",
       "Generators",
       "A flat heightfield, useful as a base or blend target.",
       {},
       {field("field")},
       {number("height", "Height", 0, "Elevation in world units.")}},
      {"elevation_mask",
       "Elevation mask",
       "Masks",
       "Select heights within a band, with optional soft edges.",
       {field("field")},
       {{"mask", "Mask", Kind::Mask}},
       {number("minimum", "Minimum height", 0, "Lower edge in world units."),
        number("maximum", "Maximum height", 20, "Upper edge in world units."),
        number("falloff", "Edge falloff", 2,
               "Width of soft edges; nonnegative.")}},
      {"slope_mask",
       "Slope mask",
       "Masks",
       "Select terrain slopes in degrees.",
       {field("field")},
       {{"mask", "Mask", Kind::Mask}},
       {number("minimum", "Minimum slope", 20, "Degrees from horizontal."),
        number("maximum", "Maximum slope", 90, "Degrees from horizontal."),
        number("falloff", "Edge falloff", 5,
               "Soft transition width in degrees.")}},
      {"height_blend",
       "Height Blend",
       "Modifiers",
       "Blend two heightfields with a scalar or connected mask.",
       {field("a"), field("b"), {"mask", "Mask", Kind::Mask, false}},
       {field("field")},
       {number("amount", "Blend amount", .5,
               "Blend from A to B, from zero to one. A mask multiplies this "
               "value.")}},
      {"offset",
       "Height Offset",
       "Modifiers",
       "Raise or lower the input, optionally through a mask.",
       {field("field"), {"mask", "Mask", Kind::Mask, false}},
       {field("field")},
       {number("amount", "Height offset", 5, "Signed offset in world units.")}},
      {"drainage",
       "Drainage",
       "Simulation",
       "Compute flow directions and basins for this surface.",
       {field("field")},
       {{"drainage", "Drainage", Kind::Drainage}},
       {number("sea_level", "Sea level", 0,
               "Water level; zero and negative levels are valid.")}},
      {"erosion",
       "Erosion",
       "Simulation",
       "Erode the input surface before manual sculpt layers.",
       {field("field"), {"drainage", "Drainage", Kind::Drainage, false}},
       {field("field")},
       {integer("iterations", "Iterations", 40,
                "Simulation iterations; nonnegative."),
        number("thermal_strength", "Thermal strength", .35,
               "Thermal material transport strength.")}},
      {"biomes",
       "Biome Rules",
       "Appearance",
       "Apply the terrain's biome rules and painted region overrides.",
       {field("field")},
       {field("field")},
       {}},
      {"water",
       "Water Body",
       "Water",
       "Carve a lake, ocean or river bed and produce separate water data.",
       {field("field"), {"water", "Existing water", Kind::Water, false}},
       {field("field"), {"water", "Water", Kind::Water}},
       {{"kind",
         "Body kind",
         "Water body shape.",
         Parameter::Choice,
         "lake",
         {"lake", "ocean", "river"},
         {},
         {},
         false},
        number("level", "Water level", 0,
               "Surface elevation; independent of render detail."),
        number("center_x", "Center X", 64, "Terrain-local position."),
        number("center_z", "Center Z", 64, "Terrain-local position."),
        number(
            "radius", "Radius", 20,
            "Maximum footprint radius. Zero removes the bound; lakes keep only the basin connected to their centre."),
        number("river_width", "River width", 4,
               "Channel width in world units."),
        {"shallow_color",
         "Shallow RGBA",
         "Colour and opacity at the shoreline.",
         Parameter::Color,
         nlohmann::json::array({0.18, 0.48, 0.55, 0.15}),
         {},
         {},
         {},
         false},
        {"deep_color",
         "Deep RGBA",
         "Colour and opacity in deep water.",
         Parameter::Color,
         nlohmann::json::array({0.02, 0.12, 0.24, 0.9}),
         {},
         {},
         {},
         false},
        {"absorption_distance",
         "Absorption distance",
         "Depth in terrain-local units for the shallow contribution to fall to "
         "37%; positive.",
         Parameter::Number,
         3.0,
         {},
         0.0,
         {},
         true},
        {"roughness",
         "Roughness",
         "Direct-light specular roughness, zero to one.",
         Parameter::Number,
         0.12,
         {},
         0.0,
         1.0,
         false},
        {"river_path",
         "River path",
         "Terrain-local XYZ bed points.",
         Parameter::Structured,
         nlohmann::json::array({{10, -1, 10}, {100, -1, 100}}),
         {},
         {},
         {},
         false}}},
      {"scatter",
       "Scatter Palette",
       "Placement",
       "Generate stable asset placements from the terrain palette.",
       {field("field")},
       {{"instances", "Instances", Kind::Instances}},
       {}},
      {"output",
       "Terrain Output",
       "Output",
       "Publish the generated base, then replay manual edits and protection "
       "layers.",
       {field("field"),
        {"water", "Water", Kind::Water, false},
        {"instances", "Instances", Kind::Instances, false}},
       {field("field")},
       {}}};
  // These are mathematical input domains, not terrain content budgets.
  for (auto &definition : definitions) {
    for (auto &parameter : definition.parameters) {
      const auto &name = parameter.name;
      if (name == "height_variation" || name == "falloff" || name == "radius" ||
          name == "iterations") {
        parameter.minimum = 0;
      } else if (name == "feature_size" || name == "river_width") {
        parameter.minimum = 0;
        parameter.exclusiveMinimum = true;
      } else if (name == "roughness" || name == "thermal_strength" ||
                 (definition.type == "height_blend" && name == "amount")) {
        parameter.minimum = 0;
        parameter.maximum = 1;
      } else if (name == "octaves") {
        parameter.minimum = 1;
      }
    }
  }
  return definitions;
}
const auto definitions = makeDefinitions();
} // namespace
std::span<const TerrainGraphNodeDefinition> terrainGraphNodeDefinitions() {
  return definitions;
}
const TerrainGraphNodeDefinition *
terrainGraphNodeDefinition(std::string_view type) {
  for (const auto &definition : definitions)
    if (definition.type == type)
      return &definition;
  return nullptr;
}
std::string_view terrainGraphValueKindName(TerrainGraphValueKind kind) {
  switch (kind) {
  case Kind::Field:
    return "Heightfield";
  case Kind::Mask:
    return "Mask";
  case Kind::Drainage:
    return "Drainage";
  case Kind::Water:
    return "Water";
  case Kind::Instances:
    return "Instances";
  }
  return "Unknown";
}
} // namespace demi::runtime
