#include "demi/runtime/terrain/TerrainGraph.h"

#include "demi/graph/DependencyGraph.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::invalid_argument(message);
}
void checkKeys(const nlohmann::json &value,
               std::initializer_list<std::string_view> allowed,
               const std::string &context) {
  require(value.is_object(), context + " must be an object");
  for (const auto &[name, entry] : value.items()) {
    require(std::ranges::find(allowed, name) != allowed.end(),
            context + ": unknown field " + name);
  }
}
std::string id(const nlohmann::json &value, const char *key) {
  require(value.contains(key) && value.at(key).is_string(),
          std::string("Terrain graph requires ") + key);
  auto result = value.at(key).get<std::string>();
  require(!result.empty(),
          std::string("Terrain graph ") + key + " must not be empty");
  return result;
}
const TerrainGraphPort *port(const std::vector<TerrainGraphPort> &ports,
                             std::string_view name) {
  const auto found = std::ranges::find(ports, name, &TerrainGraphPort::name);
  return found == ports.end() ? nullptr : &*found;
}
void validateParameters(const TerrainGraphNode &node) {
  const auto *definition = terrainGraphNodeDefinition(node.type);
  require(definition, "Node " + node.id + ": unknown module " + node.type);
  require(node.parameters.is_object(),
          "Node " + node.id + ": parameters must be an object");
  for (const auto &[name, value] : node.parameters.items()) {
    const auto found = std::ranges::find(definition->parameters, name,
                                         &TerrainGraphParameter::name);
    require(found != definition->parameters.end(),
            "Node " + node.id + ": unknown parameter " + name);
    const auto error = "Node " + node.id + ": invalid " + name;
    switch (found->kind) {
    case TerrainGraphParameterKind::Number:
      require(value.is_number() && std::isfinite(value.get<double>()), error);
      break;
    case TerrainGraphParameterKind::Integer:
      require(value.is_number_integer() &&
                  value.get<long double>() >= INT32_MIN &&
                  value.get<long double>() <= INT32_MAX,
              error);
      break;
    case TerrainGraphParameterKind::Boolean:
      require(value.is_boolean(), error);
      break;
    case TerrainGraphParameterKind::Text:
      require(value.is_string(), error);
      break;
    case TerrainGraphParameterKind::Choice:
      require(value.is_string() &&
                  std::ranges::find(found->choices, value.get<std::string>()) !=
                      found->choices.end(),
              error);
      break;
    case TerrainGraphParameterKind::Structured:
      require(value.is_object() || value.is_array(), error);
      break;
    case TerrainGraphParameterKind::Color:
      require(value.is_array() && value.size() == 4, error);
      for (const auto &channel : value)
        require(channel.is_number() && std::isfinite(channel.get<double>()) &&
                    channel.get<double>() >= 0 && channel.get<double>() <= 1,
                error + ": expected normalized RGBA");
      break;
    }
    if (value.is_number()) {
      const double number = value.get<double>();
      if (found->minimum) {
        require(found->exclusiveMinimum ? number > *found->minimum
                                        : number >= *found->minimum,
                error + ": below its allowed domain");
      }
      if (found->maximum)
        require(number <= *found->maximum,
                error + ": above its allowed domain");
    }
  }
  if (node.type == "elevation_mask" || node.type == "slope_mask") {
    const auto &parameters = node.parameters;
    const double minimum =
        parameters.value("minimum", node.type == "slope_mask" ? 20.0 : 0.0);
    const double maximum =
        parameters.value("maximum", node.type == "slope_mask" ? 90.0 : 20.0);
    require(minimum <= maximum,
            "Node " + node.id + ": minimum exceeds maximum");
  }
}
} // namespace

const TerrainGraphNode *TerrainGraph::node(std::string_view id) const {
  const auto found = std::ranges::find(nodes, id, &TerrainGraphNode::id);
  return found == nodes.end() ? nullptr : &*found;
}
const TerrainGraphLink *TerrainGraph::input(std::string_view node,
                                            std::string_view port) const {
  const auto found = std::ranges::find_if(links, [&](const auto &link) {
    return link.to.node == node && link.to.port == port;
  });
  return found == links.end() ? nullptr : &*found;
}

std::vector<std::string>
TerrainGraph::executionOrder(bool requireInputs) const {
  graph::DependencyGraph dependencies;
  for (const auto &entry : nodes)
    require(dependencies.addNode(entry.id),
            "Duplicate terrain graph node ID: " + entry.id);
  for (const auto &link : links) {
    require(dependencies.addDependency(link.to.node, link.from.node),
            "Link " + link.id + ": referenced node is missing");
  }
  auto order = dependencies.topologicalOrder();
  require(!order.blockedNode, "Terrain graph contains a cycle");
  const auto reachable = dependencies.dependenciesReachable({output});
  auto &sorted = order.nodes;
  std::erase_if(sorted,
                [&](const auto &entry) { return !reachable.contains(entry); });
  if (!requireInputs)
    return sorted;
  for (const auto &entry : sorted) {
    const auto *definition = terrainGraphNodeDefinition(node(entry)->type);
    for (const auto &required : definition->inputs)
      require(!required.required || input(entry, required.name),
              "Node " + entry + ": connect " + required.label + " input");
  }
  return sorted;
}

TerrainGraph TerrainGraph::parse(const nlohmann::json &json,
                                 bool allowIncomplete) {
  require(json.is_object(), "Terrain graph must be an object");
  checkKeys(json, {"format_version", "nodes", "links", "output"},
            "Terrain graph");
  require(json.contains("format_version") &&
              json.at("format_version").is_number_integer() &&
              json.at("format_version") == formatVersion,
          "Unsupported terrain graph format_version");
  require(json.contains("nodes") && json.at("nodes").is_array(),
          "Terrain graph nodes must be an array");
  require(json.contains("links") && json.at("links").is_array(),
          "Terrain graph links must be an array");
  TerrainGraph graph;
  if (allowIncomplete) {
    require(json.contains("output") && json.at("output").is_string(),
            "Terrain graph output must be a node ID");
    graph.output = json.at("output").get<std::string>();
  } else {
    graph.output = id(json, "output");
  }
  std::set<std::string> nodeIds, linkIds;
  for (const auto &entry : json.at("nodes")) {
    TerrainGraphNode node;
    node.id = id(entry, "id");
    checkKeys(entry, {"id", "type", "parameters", "position"},
              "Node " + node.id);
    node.type = id(entry, "type");
    require(nodeIds.insert(node.id).second,
            "Duplicate terrain graph node ID: " + node.id);
    node.parameters = entry.value("parameters", nlohmann::json::object());
    if (entry.contains("position")) {
      const auto &position = entry.at("position");
      require(position.is_array() && position.size() == 2,
              "Node " + node.id + ": position requires two coordinates");
      node.position = {position[0].get<float>(), position[1].get<float>()};
      require(std::isfinite(node.position.x) && std::isfinite(node.position.y),
              "Node " + node.id + ": invalid position");
    }
    validateParameters(node);
    graph.nodes.push_back(std::move(node));
  }
  const auto *output = graph.node(graph.output);
  require((allowIncomplete && graph.output.empty()) ||
              (output && output->type == "output"),
          "Terrain graph output must name a Terrain Output node");
  std::set<std::pair<std::string, std::string>> connectedInputs;
  for (const auto &entry : json.at("links")) {
    TerrainGraphLink link;
    link.id = id(entry, "id");
    checkKeys(entry, {"id", "from", "to"}, "Link " + link.id);
    checkKeys(entry.at("from"), {"node", "port"},
              "Link " + link.id + " source");
    checkKeys(entry.at("to"), {"node", "port"}, "Link " + link.id + " target");
    require(linkIds.insert(link.id).second,
            "Duplicate terrain graph link ID: " + link.id);
    link.from = {id(entry.at("from"), "node"), id(entry.at("from"), "port")};
    link.to = {id(entry.at("to"), "node"), id(entry.at("to"), "port")};
    const auto *source = graph.node(link.from.node);
    const auto *target = graph.node(link.to.node);
    require(source && target,
            "Link " + link.id + ": referenced node is missing");
    const auto *from =
        port(terrainGraphNodeDefinition(source->type)->outputs, link.from.port);
    const auto *to =
        port(terrainGraphNodeDefinition(target->type)->inputs, link.to.port);
    require(from && to, "Link " + link.id + ": referenced port is missing");
    require(from->kind == to->kind,
            "Link " + link.id + ": incompatible port types");
    require(connectedInputs.emplace(link.to.node, link.to.port).second,
            "Node " + link.to.node + ": input " + link.to.port +
                " already has a connection");
    graph.links.push_back(std::move(link));
  }
  (void)graph.executionOrder(!allowIncomplete);
  return graph;
}
nlohmann::json TerrainGraph::toJson() const {
  nlohmann::json result{{"format_version", formatVersion},
                        {"output", output},
                        {"nodes", nlohmann::json::array()},
                        {"links", nlohmann::json::array()}};
  for (const auto &node : nodes)
    result["nodes"].push_back(
        {{"id", node.id},
         {"type", node.type},
         {"parameters", node.parameters},
         {"position", {node.position.x, node.position.y}}});
  for (const auto &link : links)
    result["links"].push_back(
        {{"id", link.id},
         {"from", {{"node", link.from.node}, {"port", link.from.port}}},
         {"to", {{"node", link.to.node}, {"port", link.to.port}}}});
  return result;
}
std::string TerrainGraph::contentKey() const {
  auto semantic = toJson();
  auto &nodes = semantic["nodes"];
  nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                             [](const auto &node) {
                               return node.at("type") == "comment";
                             }),
              nodes.end());
  for (auto &node : nodes) {
    node.erase("position");
    for (const auto &parameter :
         terrainGraphNodeDefinition(node.at("type").get<std::string>())
             ->parameters)
      if (!node["parameters"].contains(parameter.name))
        node["parameters"][parameter.name] = parameter.defaultValue;
  }
  std::sort(
      semantic["nodes"].begin(), semantic["nodes"].end(),
      [](const auto &a, const auto &b) { return a.at("id") < b.at("id"); });
  std::sort(
      semantic["links"].begin(), semantic["links"].end(),
      [](const auto &a, const auto &b) { return a.at("id") < b.at("id"); });
  return semantic.dump();
}
std::vector<TerrainGraphDiagnostic>
terrainGraphDiagnostics(const nlohmann::json &json, bool allowIncomplete) {
  try {
    (void)TerrainGraph::parse(json, allowIncomplete);
    return {};
  } catch (const std::exception &error) {
    return {{"", "", error.what()}};
  }
}
nlohmann::json defaultTerrainGraph() {
  TerrainGraph graph;
  graph.output = "terrain_output";
  graph.nodes = {
      {"landform", "landform", nlohmann::json::object(), {40, 80}},
      {"terrain_output", "output", nlohmann::json::object(), {400, 80}}};
  graph.links = {
      {"landform_output", {"landform", "field"}, {"terrain_output", "field"}}};
  return graph.toJson();
}
} // namespace demi::runtime
