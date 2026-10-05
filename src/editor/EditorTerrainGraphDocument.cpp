#include "editor/EditorTerrainGraphDocument.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

Json emptyGraph() {
  return {{"format_version", 1},
          {"nodes", Json::array()},
          {"links", Json::array()},
          {"output", ""}};
}

bool readGraph(const Json &recipe, Json &graph, std::string &error) {
  if (!recipe.is_object()) {
    error = "Terrain draft is not an object.";
    return false;
  }
  graph = recipe.contains("graph") && !recipe["graph"].is_null()
              ? recipe["graph"]
              : emptyGraph();
  if (!graph.is_object() || !graph.contains("nodes") ||
      !graph["nodes"].is_array() || !graph.contains("links") ||
      !graph["links"].is_array() || !graph.contains("output") ||
      !graph["output"].is_string()) {
    error = "Terrain graph must contain nodes, links and an output ID.";
    return false;
  }
  return true;
}

template <class Items> auto findById(Items &items, std::string_view id) {
  return std::find_if(items.begin(), items.end(), [&](const Json &item) {
    return item.is_object() && item.contains("id") && item["id"].is_string() &&
           item["id"].get<std::string>() == id;
  });
}

std::string nextId(const Json &items, std::string_view prefix) {
  std::unordered_set<std::string> used;
  for (const Json &item : items) {
    if (item.is_object() && item.contains("id") && item["id"].is_string())
      used.insert(item["id"].get<std::string>());
  }
  for (std::uint64_t number = 1;
       number < std::numeric_limits<std::uint64_t>::max(); ++number) {
    const std::string id = std::string(prefix) + std::to_string(number);
    if (!used.contains(id))
      return id;
  }
  return {};
}

} // namespace

void EditorTerrainGraphDocument::bind(std::string identity) {
  if (identity_ == identity)
    return;
  identity_ = std::move(identity);
  clearHistory();
}

void EditorTerrainGraphDocument::clearHistory() { history_.clear(); }

bool EditorTerrainGraphDocument::undo(Json &recipe) {
  if (!history_.canUndo() || !recipe.is_object())
    return false;
  Json current;
  std::string error;
  if (!readGraph(recipe, current, error) ||
      history_.undo(current) != EditorHistoryResult::Applied) {
    clearHistory();
    return false;
  }
  if (current.is_null())
    recipe.erase("graph");
  else
    recipe["graph"] = std::move(current);
  return true;
}

bool EditorTerrainGraphDocument::redo(Json &recipe) {
  if (!history_.canRedo() || !recipe.is_object())
    return false;
  Json current = recipe.value("graph", Json{});
  if (history_.redo(current) != EditorHistoryResult::Applied) {
    clearHistory();
    return false;
  }
  recipe["graph"] = std::move(current);
  return true;
}

bool EditorTerrainGraphDocument::commit(Json &recipe, Json graph) {
  const Json before = recipe.value("graph", Json{});
  if (before == graph)
    return false;
  (void)history_.record(before, graph);
  recipe["graph"] = std::move(graph);
  return true;
}

bool EditorTerrainGraphDocument::replace(Json &recipe, Json graph,
                                         std::string &error) {
  if (!recipe.is_object()) {
    error = "Terrain draft is not an object.";
    return false;
  }
  Json wrapper = {{"graph", graph}};
  Json checked;
  if (!readGraph(wrapper, checked, error))
    return false;
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::addNode(Json &recipe, std::string_view type,
                                         Json parameters, float x, float y,
                                         std::string &nodeId,
                                         std::string &error,
                                         bool chooseOutputWhenEmpty) {
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  if (!runtime::terrainGraphNodeDefinition(type) || !parameters.is_object() ||
      !std::isfinite(x) || !std::isfinite(y)) {
    error = "A node requires a type, parameter object and finite position.";
    return false;
  }
  nodeId = nextId(graph["nodes"], "node_");
  graph["nodes"].push_back({{"id", nodeId},
                            {"type", std::string(type)},
                            {"parameters", std::move(parameters)},
                            {"position", Json::array({x, y})}});
  if (chooseOutputWhenEmpty && type == "output" && graph["output"] == "")
    graph["output"] = nodeId;
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::duplicateNode(Json &recipe,
                                               std::string_view nodeId,
                                               std::string &newNodeId,
                                               std::string &error) {
  std::vector<std::string> copies;
  if (!duplicateSelection(recipe, {std::string(nodeId)}, copies, error))
    return false;
  newNodeId = copies.front();
  return true;
}

std::optional<Json> EditorTerrainGraphDocument::copySelection(
    const Json &recipe, const std::vector<std::string> &nodeIds,
    std::string &error) const {
  error.clear();
  Json graph;
  if (!readGraph(recipe, graph, error))
    return std::nullopt;
  if (nodeIds.empty()) {
    error = "Select terrain graph nodes to copy.";
    return std::nullopt;
  }
  try {
    const std::unordered_set<std::string> selected(nodeIds.begin(),
                                                   nodeIds.end());
    Json subgraph = emptyGraph();
    for (const Json &node : graph["nodes"])
      if (selected.contains(node.at("id").get<std::string>()))
        subgraph["nodes"].push_back(node);
    if (subgraph["nodes"].size() != selected.size()) {
      error = "A selected terrain graph node no longer exists.";
      return std::nullopt;
    }
    for (const Json &link : graph["links"])
      if (selected.contains(link.at("from").at("node").get<std::string>()) &&
          selected.contains(link.at("to").at("node").get<std::string>()))
        subgraph["links"].push_back(link);
    if (selected.contains(graph["output"].get<std::string>()))
      subgraph["output"] = graph["output"];
    // Cut must never delete a selection that cannot later be pasted.
    (void)runtime::TerrainGraph::parse(subgraph, true);
    return subgraph;
  } catch (const std::exception &exception) {
    error = std::string("Cannot copy terrain graph: ") + exception.what();
    return std::nullopt;
  }
}

bool EditorTerrainGraphDocument::pasteSelection(
    Json &recipe, const Json &subgraph, float offsetX, float offsetY,
    std::vector<std::string> &newNodeIds, std::string &error) {
  error.clear();
  newNodeIds.clear();
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  if (!std::isfinite(offsetX) || !std::isfinite(offsetY)) {
    error = "Pasted node positions must be finite.";
    return false;
  }
  try {
    (void)runtime::TerrainGraph::parse(subgraph, true);
    if (subgraph.at("nodes").empty()) {
      error = "The clipboard contains no terrain graph nodes.";
      return false;
    }
    // Reserve both source and destination IDs. Cut/paste into an empty graph
    // must still assign new identities, not reuse the deleted nodes' IDs.
    std::unordered_set<std::string> reserved;
    const std::array<const Json *, 4> existingItems{
        &graph.at("nodes"), &graph.at("links"), &subgraph.at("nodes"),
        &subgraph.at("links")};
    for (const auto *items : existingItems)
      for (const Json &item : *items)
        reserved.insert(item.at("id").get<std::string>());
    std::uint64_t nodeNumber = 1;
    std::uint64_t linkNumber = 1;
    const auto freshId = [&](std::string_view prefix, std::uint64_t &number) {
      while (number < std::numeric_limits<std::uint64_t>::max()) {
        const std::string candidate =
            std::string(prefix) + std::to_string(number++);
        if (reserved.insert(candidate).second)
          return candidate;
      }
      throw std::overflow_error("Terrain graph identity space exhausted.");
    };
    std::unordered_map<std::string, std::string> remapping;
    std::vector<std::string> pastedIds;
    for (const Json &source : subgraph.at("nodes")) {
      Json copy = source;
      const auto id = freshId("node_", nodeNumber);
      remapping.emplace(source.at("id").get<std::string>(), id);
      copy["id"] = id;
      const Json position = source.value("position", Json::array({0.0F, 0.0F}));
      copy["position"] = Json::array({position[0].get<double>() + offsetX,
                                      position[1].get<double>() + offsetY});
      graph["nodes"].push_back(std::move(copy));
      pastedIds.push_back(id);
    }
    for (const Json &source : subgraph.at("links")) {
      Json copy = source;
      copy["id"] = freshId("link_", linkNumber);
      copy["from"]["node"] =
          remapping.at(source.at("from").at("node").get<std::string>());
      copy["to"]["node"] =
          remapping.at(source.at("to").at("node").get<std::string>());
      graph["links"].push_back(std::move(copy));
    }
    const std::string sourceOutput = subgraph.at("output").get<std::string>();
    if (graph["output"] == "" && !sourceOutput.empty())
      graph["output"] = remapping.at(sourceOutput);
    (void)runtime::TerrainGraph::parse(graph, true);
    if (!commit(recipe, std::move(graph)))
      return false;
    newNodeIds = std::move(pastedIds);
    return true;
  } catch (const std::exception &exception) {
    error = std::string("Cannot paste terrain graph: ") + exception.what();
    return false;
  }
}

bool EditorTerrainGraphDocument::duplicateSelection(
    Json &recipe, const std::vector<std::string> &nodeIds,
    std::vector<std::string> &newNodeIds, std::string &error) {
  const auto copied = copySelection(recipe, nodeIds, error);
  return copied &&
         pasteSelection(recipe, *copied, 32.0F, 32.0F, newNodeIds, error);
}

bool EditorTerrainGraphDocument::removeSelection(
    Json &recipe, const std::vector<std::string> &nodeIds,
    const std::vector<std::string> &linkIds, std::string &error) {
  error.clear();
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  const std::unordered_set<std::string> nodes(nodeIds.begin(), nodeIds.end());
  const std::unordered_set<std::string> links(linkIds.begin(), linkIds.end());
  for (const auto &id : nodes)
    if (findById(graph["nodes"], id) == graph["nodes"].end()) {
      error = "A selected terrain graph node no longer exists.";
      return false;
    }
  for (const auto &id : links)
    if (findById(graph["links"], id) == graph["links"].end()) {
      error = "A selected terrain graph link no longer exists.";
      return false;
    }
  auto &graphNodes = graph["nodes"];
  for (auto node = graphNodes.begin(); node != graphNodes.end();)
    node = nodes.contains(node->at("id").get<std::string>())
               ? graphNodes.erase(node)
               : std::next(node);
  auto &graphLinks = graph["links"];
  for (auto link = graphLinks.begin(); link != graphLinks.end();) {
    const bool remove =
        links.contains(link->at("id").get<std::string>()) ||
        nodes.contains(link->at("from").at("node").get<std::string>()) ||
        nodes.contains(link->at("to").at("node").get<std::string>());
    link = remove ? graphLinks.erase(link) : std::next(link);
  }
  if (nodes.contains(graph["output"].get<std::string>()))
    graph["output"] = "";
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::removeNode(Json &recipe,
                                            std::string_view nodeId,
                                            std::string &error) {
  return removeSelection(recipe, {std::string(nodeId)}, {}, error);
}

bool EditorTerrainGraphDocument::connect(
    Json &recipe, std::string_view fromNode, std::string_view fromPort,
    std::string_view toNode, std::string_view toPort, std::string &error) {
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  graph["links"].push_back(
      {{"id", nextId(graph["links"], "link_")},
       {"from",
        {{"node", std::string(fromNode)}, {"port", std::string(fromPort)}}},
       {"to", {{"node", std::string(toNode)}, {"port", std::string(toPort)}}}});
  try {
    (void)runtime::TerrainGraph::parse(graph, true);
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::setInputSource(
    Json &recipe, std::string_view toNode, std::string_view toPort,
    std::string_view fromNode, std::string_view fromPort, std::string &error) {
  error.clear();
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  try {
    const auto parsed = runtime::TerrainGraph::parse(graph, true);
    const auto *target = parsed.node(toNode);
    if (target == nullptr) {
      error = "Terrain graph target node no longer exists.";
      return false;
    }
    const auto *definition = runtime::terrainGraphNodeDefinition(target->type);
    const bool isInput =
        std::any_of(definition->inputs.begin(), definition->inputs.end(),
                    [toPort](const auto &port) { return port.name == toPort; });
    if (!isInput) {
      error = "Terrain graph target input port no longer exists.";
      return false;
    }
    const auto *current = parsed.input(toNode, toPort);
    if (fromNode.empty()) {
      if (current == nullptr)
        return false;
      graph["links"].erase(findById(graph["links"], current->id));
    } else {
      if (current && current->from.node == fromNode &&
          current->from.port == fromPort)
        return false;
      Json source{{"node", std::string(fromNode)},
                  {"port", std::string(fromPort)}};
      if (current) {
        auto link = findById(graph["links"], current->id);
        (*link)["from"] = std::move(source);
      } else {
        graph["links"].push_back(
            {{"id", nextId(graph["links"], "link_")},
             {"from", std::move(source)},
             {"to",
              {{"node", std::string(toNode)}, {"port", std::string(toPort)}}}});
      }
    }
    // The native parser validates output direction, types and all cycles.
    // A disconnected required input is permitted in an unapplied draft.
    (void)runtime::TerrainGraph::parse(graph, true);
    return commit(recipe, std::move(graph));
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorTerrainGraphDocument::removeLink(Json &recipe,
                                            std::string_view linkId,
                                            std::string &error) {
  return removeSelection(recipe, {}, {std::string(linkId)}, error);
}

bool EditorTerrainGraphDocument::setParameter(Json &recipe,
                                              std::string_view nodeId,
                                              std::string_view name, Json value,
                                              std::string &error) {
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  const auto node = findById(graph["nodes"], nodeId);
  if (node == graph["nodes"].end() || name.empty()) {
    error = "Terrain graph node or parameter no longer exists.";
    return false;
  }
  if (!node->contains("parameters") || !(*node)["parameters"].is_object()) {
    error = "Terrain graph node parameters are malformed.";
    return false;
  }
  (*node)["parameters"][std::string(name)] = std::move(value);
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::setPosition(Json &recipe,
                                             std::string_view nodeId, float x,
                                             float y, std::string &error) {
  return setPositions(recipe, {{std::string(nodeId), x, y}}, error);
}

bool EditorTerrainGraphDocument::setPositions(
    Json &recipe, const std::vector<NodePosition> &positions,
    std::string &error) {
  error.clear();
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  for (const auto &position : positions) {
    const auto node = findById(graph["nodes"], position.nodeId);
    if (node == graph["nodes"].end() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
      error = "Terrain graph node must exist and its position must be finite.";
      return false;
    }
    (*node)["position"] = Json::array({position.x, position.y});
  }
  return commit(recipe, std::move(graph));
}

bool EditorTerrainGraphDocument::setOutput(Json &recipe,
                                           std::string_view nodeId,
                                           std::string &error) {
  Json graph;
  if (!readGraph(recipe, graph, error))
    return false;
  graph["output"] = std::string(nodeId);
  try {
    (void)runtime::TerrainGraph::parse(graph, true);
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
  return commit(recipe, std::move(graph));
}

} // namespace demi::editor
