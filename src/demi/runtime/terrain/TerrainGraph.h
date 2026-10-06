#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace demi::runtime {
struct TerrainGraphNode {
  std::string id;
  std::string type;
  nlohmann::json parameters = nlohmann::json::object();
  Vec2 position{};
};
struct TerrainGraphEndpoint {
  std::string node, port;
};
struct TerrainGraphLink {
  std::string id;
  TerrainGraphEndpoint from, to;
};
struct TerrainGraph {
  static constexpr int formatVersion = 1;
  std::vector<TerrainGraphNode> nodes;
  std::vector<TerrainGraphLink> links;
  std::string output;
  // Incomplete drafts may lack an output/required inputs; links and cycles
  // still use the same native validation as generated graphs.
  static TerrainGraph parse(const nlohmann::json &json,
                            bool allowIncomplete = false);
  nlohmann::json toJson() const;
  const TerrainGraphNode *node(std::string_view id) const;
  const TerrainGraphLink *input(std::string_view node,
                                std::string_view port) const;
  std::vector<std::string> executionOrder(bool requireInputs = true) const;
  // Visual node positions are excluded from the generator/cache identity.
  std::string contentKey() const;
};
struct TerrainGraphDiagnostic {
  std::string node, port, message;
};
std::vector<TerrainGraphDiagnostic>
terrainGraphDiagnostics(const nlohmann::json &graph,
                        bool allowIncomplete = false);
// A readable source -> output graph, suitable for editing or a preset template.
nlohmann::json defaultTerrainGraph();
} // namespace demi::runtime
