#include "demi/runtime/terrain/TerrainGraph.h"
#include "editor/EditorTerrainGraphDocument.h"

#include <array>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;
using demi::editor::EditorTerrainGraphDocument;
using demi::runtime::TerrainGraph;

bool requiresCompletion(const Json &graph) {
  try {
    (void)TerrainGraph::parse(graph);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

void incompleteDraftCanBeWired() {
  EditorTerrainGraphDocument document;
  document.bind("scene-a#terrain");
  Json recipe = {{"seed", 71}};
  std::string error, source, output;
  assert(document.addNode(recipe, "landform", Json::object(), 40, 80, source,
                          error));
  assert(document.addNode(recipe, "output", Json::object(), 400, 80, output,
                          error));
  assert(source != output);
  assert(recipe.at("seed") == 71);
  assert(requiresCompletion(recipe.at("graph")));
  (void)TerrainGraph::parse(recipe.at("graph"), true);

  assert(document.connect(recipe, source, "field", output, "field", error));
  assert(recipe.at("graph").at("links").size() == 1);
  assert(recipe.at("graph").at("output") == "");
  const Json beforeOutput = recipe;
  assert(!document.setOutput(recipe, source, error));
  assert(error.find("output must name a Terrain Output node") !=
         std::string::npos);
  assert(recipe == beforeOutput);
  assert(document.setOutput(recipe, output, error));
  (void)TerrainGraph::parse(recipe.at("graph"));

  assert(document.undo(recipe));
  assert(recipe.at("graph").at("output") == "");
  assert(document.undo(recipe));
  assert(recipe.at("graph").at("links").empty());
  assert(document.redo(recipe));
  assert(document.redo(recipe));
  assert(recipe.at("graph").at("output") == output);
  assert(recipe.at("seed") == 71);

  document.bind("scene-b#terrain");
  assert(!document.canUndo());
  assert(!document.canRedo());
  assert(recipe.at("graph").at("output") == output);
}

void nativeLinkRulesLeaveRejectedDraftUntouched() {
  EditorTerrainGraphDocument document;
  document.bind("scene#terrain");
  Json recipe = {{"graph", demi::runtime::defaultTerrainGraph()}};
  std::string error, mask, constant;
  assert(document.addNode(recipe, "elevation_mask", Json::object(), 100, 200,
                          mask, error));
  assert(document.addNode(recipe, "constant", Json::object(), 200, 200,
                          constant, error));
  document.clearHistory();
  const Json before = recipe;

  assert(!document.connect(recipe, mask, "mask", "terrain_output", "field",
                           error));
  assert(error.find("incompatible port types") != std::string::npos);
  assert(recipe == before && !document.canUndo());

  error.clear();
  assert(!document.connect(recipe, constant, "field", "terrain_output", "field",
                           error));
  assert(error.find("already has a connection") != std::string::npos);
  assert(recipe == before && !document.canUndo());

  error.clear();
  assert(
      !document.connect(recipe, "landform", "missing", mask, "field", error));
  assert(error.find("referenced port is missing") != std::string::npos);
  assert(recipe == before && !document.canUndo());

  Json cyclic = Json::object();
  EditorTerrainGraphDocument cycleDocument;
  cycleDocument.bind("scene#cycle");
  std::string a, b;
  assert(
      cycleDocument.addNode(cyclic, "offset", Json::object(), 0, 0, a, error));
  assert(cycleDocument.addNode(cyclic, "offset", Json::object(), 100, 0, b,
                               error));
  assert(cycleDocument.connect(cyclic, a, "field", b, "field", error));
  const Json beforeCycle = cyclic;
  error.clear();
  assert(!cycleDocument.connect(cyclic, b, "field", a, "field", error));
  assert(error.find("cycle") != std::string::npos);
  assert(cyclic == beforeCycle);
}

void invalidParameterCanBeCorrectedBeforeWiring() {
  EditorTerrainGraphDocument document;
  document.bind("scene#invalid-parameter");
  Json recipe = Json::object();
  std::string error, noise, output;
  assert(document.addNode(recipe, "noise", {{"feature_size", 0}}, 0, 0, noise,
                          error));
  assert(document.addNode(recipe, "output", Json::object(), 250, 0, output,
                          error));
  const Json invalidDraft = recipe;
  assert(!document.connect(recipe, noise, "field", output, "field", error));
  assert(error.find("invalid feature_size") != std::string::npos);
  assert(recipe == invalidDraft);

  assert(document.setParameter(recipe, noise, "feature_size", 2, error));
  assert(document.connect(recipe, noise, "field", output, "field", error));
  assert(document.setOutput(recipe, output, error));
  (void)TerrainGraph::parse(recipe.at("graph"));
}

void sourceEditsAndHistoryStayInTheDraft() {
  EditorTerrainGraphDocument document;
  document.bind("scene#terrain");
  Json recipe = {{"seed", 11}, {"graph", demi::runtime::defaultTerrainGraph()}};
  const Json originalGraph = recipe.at("graph");
  std::string error, duplicate;

  assert(document.duplicateNode(recipe, "landform", duplicate, error));
  assert(duplicate != "landform");
  assert(recipe.at("graph").at("links") == originalGraph.at("links"));
  assert(document.setPosition(recipe, duplicate, 120, 130, error));
  assert(document.removeLink(recipe, "landform_output", error));
  assert(recipe.at("graph").at("links").empty());
  assert(document.undo(recipe));
  assert(recipe.at("graph").at("links") == originalGraph.at("links"));
  assert(document.redo(recipe));
  assert(recipe.at("graph").at("links").empty());
  assert(document.undo(recipe));
  assert(recipe.at("graph").at("links") == originalGraph.at("links"));

  assert(document.removeNode(recipe, "terrain_output", error));
  assert(recipe.at("graph").at("output") == "");
  assert(recipe.at("graph").at("links").empty());
  assert(document.undo(recipe));
  assert(recipe.at("graph").at("output") == "terrain_output");
  assert(recipe.at("graph").at("links") == originalGraph.at("links"));
  assert(recipe.at("seed") == 11);

  recipe["graph"]["nodes"][0]["position"] = {777, 888};
  assert(!document.undo(recipe));
  assert(!document.canUndo() && !document.canRedo());
  assert(recipe.at("graph").at("nodes")[0]["position"] ==
         Json::array({777, 888}));

  Json withoutGraph = {{"seed", 13}};
  EditorTerrainGraphDocument fresh;
  fresh.bind("scene#new-terrain");
  assert(
      fresh.replace(withoutGraph, demi::runtime::defaultTerrainGraph(), error));
  assert(fresh.undo(withoutGraph));
  assert(!withoutGraph.contains("graph"));
  assert(fresh.redo(withoutGraph));
  assert(withoutGraph.at("graph") == demi::runtime::defaultTerrainGraph());
}

Json clipboardRecipe() {
  return {
      {"seed", 73},
      {"graph",
       {{"format_version", 1},
        {"nodes",
         Json::array({{{"id", "noise"},
                       {"type", "noise"},
                       {"parameters",
                        {{"roughness", 0.73123456789}, {"seed_offset", 19}}},
                       {"position", {-123.25, 87.75}}},
                      {{"id", "water"},
                       {"type", "water"},
                       {"parameters",
                        {{"kind", "river"},
                         {"river_path",
                          Json::array({{0.0, 1.5, -8.0}, {8.25, 1.25, 7.0}})}}},
                       {"position", {221.5, -49.125}}},
                      {{"id", "terrain_output"},
                       {"type", "output"},
                       {"parameters", Json::object()},
                       {"position", {800, 100}}}})},
        {"links",
         Json::array(
             {{{"id", "noise_water"},
               {"from", {{"node", "noise"}, {"port", "field"}}},
               {"to", {{"node", "water"}, {"port", "field"}}}},
              {{"id", "water_output"},
               {"from", {{"node", "water"}, {"port", "field"}}},
               {"to", {{"node", "terrain_output"}, {"port", "field"}}}}})},
        {"output", "terrain_output"}}}};
}

void copyPastePreservesTypedSubgraphsAndHistory() {
  EditorTerrainGraphDocument document;
  document.bind("scene#clipboard");
  Json recipe = clipboardRecipe();
  const Json before = recipe;
  std::string error;
  const auto copied = document.copySelection(recipe, {"noise", "water"}, error);
  assert(copied && error.empty());
  assert(copied->at("nodes").size() == 2);
  assert(copied->at("links").size() == 1);
  assert(copied->at("output") == "");
  assert(recipe == before && !document.canUndo());
  std::vector<std::string> newIds;
  assert(document.pasteSelection(recipe, *copied, 32, 48, newIds, error));
  assert(newIds.size() == 2 && newIds[0] != "noise" && newIds[1] != "water");
  const auto &graph = recipe.at("graph");
  assert(graph.at("nodes").size() == 5 && graph.at("links").size() == 3);
  assert(graph.at("output") == before.at("graph").at("output"));
  for (std::size_t index = 0; index < 2; ++index) {
    const auto &original = copied->at("nodes")[index];
    const auto &pasted = graph.at("nodes")[index + 3];
    assert(pasted.at("type") == original.at("type"));
    assert(pasted.at("parameters") == original.at("parameters"));
    assert(pasted.at("position")[0] ==
           original.at("position")[0].get<double>() + 32);
    assert(pasted.at("position")[1] ==
           original.at("position")[1].get<double>() + 48);
  }
  const Json &link = graph.at("links").back();
  assert(link.at("id") != copied->at("links")[0].at("id"));
  assert(link.at("from").at("node") == newIds[0]);
  assert(link.at("to").at("node") == newIds[1]);
  assert(link.at("from").at("port") == "field");
  (void)TerrainGraph::parse(graph);
  const Json afterPaste = recipe;
  assert(document.undo(recipe) && recipe == before);
  assert(!document.canUndo());
  assert(document.redo(recipe) && recipe == afterPaste);
}

void duplicateAndDeleteUseOneTransaction() {
  EditorTerrainGraphDocument document;
  document.bind("scene#batch");
  Json recipe = clipboardRecipe();
  const Json before = recipe;
  std::vector<std::string> copies;
  std::string error;
  assert(
      document.duplicateSelection(recipe, {"noise", "water"}, copies, error));
  assert(recipe.at("graph").at("links").size() == 3);
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
  assert(document.removeSelection(recipe, {"noise", "water"}, {}, error));
  assert(recipe.at("graph").at("nodes").size() == 1);
  assert(recipe.at("graph").at("links").empty());
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
  assert(document.removeSelection(recipe, {}, {"noise_water", "water_output"},
                                  error));
  assert(recipe.at("graph").at("nodes") == before.at("graph").at("nodes"));
  assert(recipe.at("graph").at("links").empty());
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
  assert(document.removeSelection(recipe, {"noise", "water", "terrain_output"},
                                  {}, error));
  assert(recipe.at("graph").at("output") == "");
  assert(document.undo(recipe) && recipe == before);
}

void copiedGraphCanCrossDocumentsWithoutReusingIds() {
  EditorTerrainGraphDocument document;
  Json source = clipboardRecipe();
  const Json before = source;
  std::string error;
  auto copied = document.copySelection(
      source, {"noise", "water", "terrain_output"}, error);
  assert(copied);
  Json destination = {{"seed", 19}};
  const Json destinationBefore = destination;
  document.bind("scene#destination");
  std::vector<std::string> newIds;
  assert(document.pasteSelection(destination, *copied, 0, 0, newIds, error));
  assert(source == before && destination.at("seed") == 19);
  assert(destination.at("graph").at("output") == newIds[2]);
  (void)TerrainGraph::parse(destination.at("graph"));
  assert(document.undo(destination) && destination == destinationBefore);

  // Numeric-looking source IDs remain reserved even after cutting everything.
  Json numbered = *copied;
  numbered["nodes"][0]["id"] = "node_1";
  numbered["links"][0]["from"]["node"] = "node_1";
  assert(document.pasteSelection(destination, numbered, 0, 0, newIds, error));
  assert(newIds[0] != "node_1");
}

void malformedClipboardAndFailedBatchesLeaveDraftUntouched() {
  EditorTerrainGraphDocument document;
  Json recipe = clipboardRecipe();
  const Json before = recipe;
  std::string error;
  std::vector<std::string> ids;
  assert(!document.pasteSelection(recipe, Json::object(), 32, 32, ids, error));
  assert(!error.empty() && ids.empty() && recipe == before &&
         !document.canUndo());
  Json invalid = before.at("graph");
  invalid["nodes"][0]["type"] = "unknown_module";
  assert(!document.pasteSelection(recipe, invalid, 0, 0, ids, error));
  invalid = before.at("graph");
  invalid["nodes"][0]["parameters"]["roughness"] = "invalid_number";
  assert(!document.pasteSelection(recipe, invalid, 0, 0, ids, error));
  invalid = before.at("graph");
  invalid["links"][0]["from"]["port"] = "missing_port";
  assert(!document.pasteSelection(recipe, invalid, 0, 0, ids, error));
  invalid = before.at("graph");
  invalid["nodes"][1]["id"] = "noise";
  assert(!document.pasteSelection(recipe, invalid, 0, 0, ids, error));
  assert(!document.pasteSelection(recipe, before.at("graph"),
                                  std::numeric_limits<float>::infinity(), 0,
                                  ids, error));
  assert(!document.removeSelection(recipe, {"noise", "missing"}, {}, error));
  assert(!document.removeSelection(recipe, {}, {"missing_link"}, error));
  assert(!document.setPositions(
      recipe, {{"noise", 100, 100}, {"missing", 200, 200}}, error));
  assert(!document.copySelection(recipe, {"missing"}, error));
  assert(recipe == before && !document.canUndo());
  assert(document.setPositions(
      recipe, {{"noise", 100, 100}, {"water", 200, 200}}, error));
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
}

Json inputSourceRecipe() {
  Json recipe = clipboardRecipe();
  // Keep precision, positions and unrelated recipe data in the equality
  // checks: changing one input must not serialize the whole native graph.
  recipe["palette"] = "asset://terrain/vegetation";
  recipe["edits"] = Json::array({{{"kind", "raise"},
                                  {"center", {8.125, 12.75}},
                                  {"strength", 0.73123456789}}});
  auto &graph = recipe["graph"];
  graph["nodes"].push_back({{"id", "flat"},
                            {"type", "constant"},
                            {"parameters", {{"height", 4.125}}},
                            {"position", {-300.5, 141.25}}});
  graph["nodes"].push_back({{"id", "mask"},
                            {"type", "elevation_mask"},
                            {"parameters", Json::object()},
                            {"position", {91.25, -202.5}}});
  graph["links"].push_back({{"id", "noise_mask"},
                            {"from", {{"node", "noise"}, {"port", "field"}}},
                            {"to", {{"node", "mask"}, {"port", "field"}}}});
  (void)TerrainGraph::parse(graph);
  return recipe;
}

void replacingAndDisconnectingInputsUseOneTransaction() {
  EditorTerrainGraphDocument document;
  document.bind("scene#input-choice");
  Json recipe = inputSourceRecipe();
  const Json before = recipe;
  Json replaced = before;
  replaced["graph"]["links"][0]["from"]["node"] = "flat";
  std::string error;
  assert(document.setInputSource(recipe, "water", "field", "flat", "field",
                                 error));
  assert(error.empty() && recipe == replaced);
  assert(document.canUndo() && !document.canRedo());
  const auto parsed = TerrainGraph::parse(recipe.at("graph"));
  assert(parsed.input("water", "field")->id == "noise_water");
  // Drag-to-connect still rejects an occupied input instead of replacing it.
  assert(!document.connect(recipe, "noise", "field", "water", "field", error));
  assert(error.find("already has a connection") != std::string::npos);
  assert(recipe == replaced);
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
  assert(document.redo(recipe) && recipe == replaced && !document.canRedo());

  document.clearHistory();
  Json disconnected = replaced;
  disconnected["graph"]["links"].erase(disconnected["graph"]["links"].begin());
  // Empty fromNode alone requests disconnection; fromPort is irrelevant.
  assert(
      document.setInputSource(recipe, "water", "field", "", "ignored", error));
  assert(error.empty() && recipe == disconnected);
  assert(requiresCompletion(recipe.at("graph")));
  (void)TerrainGraph::parse(recipe.at("graph"), true);
  assert(document.undo(recipe) && recipe == replaced && !document.canUndo());
  assert(document.redo(recipe) && recipe == disconnected &&
         !document.canRedo());
}

void inputSourceNoOpsPreserveRedoAndDoNotCreateHistory() {
  EditorTerrainGraphDocument document;
  document.bind("scene#unchanged-input");
  Json recipe = inputSourceRecipe();
  const Json before = recipe;
  std::string error = "previous error";
  assert(!document.setInputSource(recipe, "water", "field", "noise", "field",
                                  error));
  assert(error.empty() && recipe == before && !document.canUndo() &&
         !document.canRedo());
  error = "previous error";
  assert(!document.setInputSource(recipe, "terrain_output", "water", "", "",
                                  error));
  assert(error.empty() && recipe == before && !document.canUndo() &&
         !document.canRedo());

  assert(document.setInputSource(recipe, "water", "field", "flat", "field",
                                 error));
  const Json replaced = recipe;
  assert(document.undo(recipe) && recipe == before && document.canRedo());
  assert(!document.setInputSource(recipe, "water", "field", "noise", "field",
                                  error));
  assert(error.empty() && document.canRedo() && !document.canUndo());
  assert(!document.setInputSource(recipe, "terrain_output", "water", "", "",
                                  error));
  assert(error.empty() && document.canRedo() && !document.canUndo());
  assert(document.redo(recipe) && recipe == replaced);
}

void connectingAnEmptyInputUsesTypedPortsAndOneUndo() {
  EditorTerrainGraphDocument document;
  document.bind("scene#optional-input");
  Json recipe = inputSourceRecipe();
  const Json before = recipe;
  std::string error;
  assert(document.setInputSource(recipe, "terrain_output", "water", "water",
                                 "water", error));
  assert(error.empty());
  auto expected = before;
  expected["graph"]["links"].push_back(
      {{"id", "link_1"},
       {"from", {{"node", "water"}, {"port", "water"}}},
       {"to", {{"node", "terrain_output"}, {"port", "water"}}}});
  assert(recipe == expected);
  const auto graph = TerrainGraph::parse(recipe.at("graph"));
  assert(graph.input("terrain_output", "water")->from.node == "water");
  assert(graph.input("terrain_output", "water")->from.port == "water");
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
  assert(document.redo(recipe) && recipe == expected);
}

void rejectedInputChoicesPreserveGraphAndHistory() {
  EditorTerrainGraphDocument document;
  document.bind("scene#rejected-input");
  Json recipe = inputSourceRecipe();
  const Json before = recipe;
  std::string error;
  assert(document.setInputSource(recipe, "water", "field", "flat", "field",
                                 error));
  const Json validReplacement = recipe;
  assert(document.undo(recipe) && recipe == before);
  struct InputChoice {
    std::string_view toNode;
    std::string_view toPort;
    std::string_view fromNode;
    std::string_view fromPort;
    std::string_view diagnostic;
  };
  const std::array choices{
      InputChoice{"water", "field", "mask", "mask", "incompatible port types"},
      InputChoice{"water", "field", "water", "field", "cycle"},
      InputChoice{"water", "field", "terrain_output", "field", "cycle"},
      InputChoice{"water", "field", "missing", "field",
                  "referenced node is missing"},
      InputChoice{"water", "field", "mask", "field",
                  "referenced port is missing"},
      InputChoice{"water", "field", "flat", "missing",
                  "referenced port is missing"},
      InputChoice{"noise", "field", "flat", "field", "input port"},
      InputChoice{"water", "missing", "flat", "field", "input port"},
      InputChoice{"missing", "field", "flat", "field", "target node"},
      InputChoice{"missing", "field", "", "", "target node"},
      InputChoice{"water", "missing", "", "", "input port"}};
  for (const auto &choice : choices) {
    assert(!document.setInputSource(recipe, choice.toNode, choice.toPort,
                                    choice.fromNode, choice.fromPort, error));
    assert(error.find(choice.diagnostic) != std::string::npos);
    assert(recipe == before && !document.canUndo() && document.canRedo());
  }
  assert(document.redo(recipe) && recipe == validReplacement);
  document.clearHistory();
  const Json valid = recipe;
  recipe["graph"]["links"][0]["from"]["port"] = "missing";
  const Json malformed = recipe;
  assert(!document.setInputSource(recipe, "water", "field", "flat", "field",
                                  error));
  assert(!error.empty() && recipe == malformed && !document.canUndo() &&
         !document.canRedo());
  recipe = valid;
  assert(!document.setInputSource(recipe, "water", "field", "flat", "field",
                                  error));
  assert(error.empty() && recipe == valid && !document.canUndo());
}

void addingTheFirstOutputIsOneTransaction() {
  EditorTerrainGraphDocument document;
  Json recipe = {{"seed", 12}};
  const Json before = recipe;
  std::string output;
  std::string error;
  assert(document.addNode(recipe, "output", Json::object(), 100, 200, output,
                          error, true));
  assert(recipe.at("graph").at("output") == output);
  assert(document.undo(recipe) && recipe == before && !document.canUndo());
}

} // namespace

void commentEditingRoundTrips() {
  EditorTerrainGraphDocument document;
  document.bind("comments");
  Json recipe{{"graph", demi::runtime::defaultTerrainGraph()}};
  const auto initial = recipe;
  std::string id, error;
  assert(document.addNode(recipe, "comment", {{"text", "Start here"}}, 10, 20,
                          id, error));
  assert(document.setParameter(recipe, id, "text",
                               "Explain the noise\nThen generate", error));
  const auto edited = recipe;
  assert(document.undo(recipe));
  assert(document.redo(recipe));
  assert(recipe == edited);
  auto copied = document.copySelection(recipe, {id}, error);
  assert(copied);
  std::vector<std::string> pasted;
  assert(document.pasteSelection(recipe, *copied, 30, 30, pasted, error));
  assert(pasted.size() == 1 && pasted.front() != id);
  assert(TerrainGraph::parse(recipe["graph"])
             .node(pasted.front())
             ->parameters["text"] == "Explain the noise\nThen generate");
  assert(document.undo(recipe));
  assert(recipe == edited);
  assert(document.undo(recipe));
  assert(document.undo(recipe));
  assert(recipe == initial);
}

int main() {
  commentEditingRoundTrips();
  incompleteDraftCanBeWired();
  nativeLinkRulesLeaveRejectedDraftUntouched();
  invalidParameterCanBeCorrectedBeforeWiring();
  sourceEditsAndHistoryStayInTheDraft();
  copyPastePreservesTypedSubgraphsAndHistory();
  duplicateAndDeleteUseOneTransaction();
  copiedGraphCanCrossDocumentsWithoutReusingIds();
  malformedClipboardAndFailedBatchesLeaveDraftUntouched();
  addingTheFirstOutputIsOneTransaction();
  replacingAndDisconnectingInputsUseOneTransaction();
  inputSourceNoOpsPreserveRedoAndDoNotCreateHistory();
  connectingAnEmptyInputUsesTypedPortsAndOneUndo();
  rejectedInputChoicesPreserveGraphAndHistory();
}
