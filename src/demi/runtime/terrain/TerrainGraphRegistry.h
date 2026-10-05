#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {
enum class TerrainGraphValueKind { Field, Mask, Drainage, Water, Instances };
enum class TerrainGraphParameterKind {
  Number,
  Integer,
  Boolean,
  Text,
  Choice,
  Structured
};
struct TerrainGraphPort {
  std::string name;
  std::string label;
  TerrainGraphValueKind kind = TerrainGraphValueKind::Field;
  bool required = true;
};
struct TerrainGraphParameter {
  std::string name;
  std::string label;
  std::string help;
  TerrainGraphParameterKind kind = TerrainGraphParameterKind::Number;
  nlohmann::json defaultValue;
  std::vector<std::string> choices;
  std::optional<double> minimum;
  std::optional<double> maximum;
  bool exclusiveMinimum = false;
};
struct TerrainGraphNodeDefinition {
  std::string type;
  std::string label;
  std::string category;
  std::string description;
  std::vector<TerrainGraphPort> inputs;
  std::vector<TerrainGraphPort> outputs;
  std::vector<TerrainGraphParameter> parameters;
};
std::span<const TerrainGraphNodeDefinition> terrainGraphNodeDefinitions();
const TerrainGraphNodeDefinition *
terrainGraphNodeDefinition(std::string_view type);
std::string_view terrainGraphValueKindName(TerrainGraphValueKind kind);
} // namespace demi::runtime
