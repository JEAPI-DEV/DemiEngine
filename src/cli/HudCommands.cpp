#include "cli/HudCommands.h"
#include "cli/CommandLine.h"

#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/ui/HudLayoutReport.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

namespace demi::cli {
namespace {

using Json = nlohmann::json;

std::optional<Json> readJson(const std::string &path, std::ostream &error) {
  std::ifstream input(path);
  if (!input) {
    error << "Failed to read HUD file: " << path << '\n';
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &e) {
    error << "Invalid JSON in " << path << ": " << e.what() << '\n';
    return std::nullopt;
  }
}

Json reportToJson(const runtime::ui::HudLayoutReport &report) {
  Json nodes = Json::array();
  for (const runtime::ui::HudNodeReport &node : report.nodes) {
    nodes.push_back({
        {"id", node.id},
        {"type", node.type},
        {"action", node.action},
        {"visible", node.visible},
        {"focusable", node.focusable},
        {"blocks_pointer", node.blocksPointer},
        {"resolved",
         {{"x", node.resolved.x},
          {"y", node.resolved.y},
          {"width", node.resolved.width},
          {"height", node.resolved.height}}},
    });
    if (node.type == "image") {
      nodes.back()["image"] = {
          {"texture", node.texture},
          {"source_position", {node.sourcePosition.x, node.sourcePosition.y}},
          {"source_size", {node.sourceSize.x, node.sourceSize.y}},
          {"nine_slice",
           {{node.imageSkin.start.x, node.imageSkin.start.y},
            {node.imageSkin.end.x, node.imageSkin.end.y}}},
          {"nine_slice_scale", node.imageSkin.scale},
      };
    }
  }
  return {{"format_version", 1},
          {"canvas_size",
           {{"width", report.canvasSize.x}, {"height", report.canvasSize.y}}},
          {"nodes", nodes}};
}

void printText(const runtime::ui::HudLayoutReport &report, std::ostream &out) {
  out << "Canvas: " << report.canvasSize.x << "x" << report.canvasSize.y
      << '\n';
  for (const runtime::ui::HudNodeReport &node : report.nodes) {
    out << node.id << " type=" << node.type;
    if (!node.action.empty())
      out << " action=" << node.action;
    out << " visible=" << (node.visible ? "true" : "false")
        << " focusable=" << (node.focusable ? "true" : "false")
        << " blocks_pointer=" << (node.blocksPointer ? "true" : "false")
        << " rect=" << node.resolved.x << "," << node.resolved.y << ","
        << node.resolved.width << "," << node.resolved.height << '\n';
  }
}

} // namespace

int runHudCommand(const std::vector<std::string> &args, std::ostream &out,
                  std::ostream &error) {
  CLI::App command{"Inspect resolved HUD layout", "demi hud"};
  command.require_subcommand(1, 1);
  auto *inspect =
      command.add_subcommand("inspect", "Report canvas and node rectangles");
  std::string path;
  std::string format = "text";
  std::vector<float> safeArea;
  bool revealHidden = false;
  inspect->add_option("path", path)->required();
  inspect->add_option("--format", format)
      ->check(CLI::IsMember({"text", "json"}));
  inspect->add_option("--safe-area", safeArea)->delimiter(',')->expected(4);
  inspect->add_flag("--reveal-hidden", revealHidden);
  if (const auto status = parseCommand(command, args, 1, out, error)) {
    return *status;
  }
  const auto document = readJson(path, error);
  if (!document)
    return 1;
  runtime::ui::HudLayoutRequest request;
  request.revealHidden = revealHidden;
  if (!safeArea.empty()) {
    request.safeArea = {safeArea[0], safeArea[1], safeArea[2], safeArea[3]};
  }
  std::string failure;
  auto hud = runtime::scene_loading::parseHudDocument(path, *document, failure);
  if (!hud) {
    error << failure << '\n';
    return 1;
  }
  const runtime::ui::HudLayoutReport report =
      runtime::ui::inspectHudLayout(std::move(*hud), request);
  if (format == "json") {
    out << reportToJson(report).dump(2) << '\n';
  } else {
    printText(report, out);
  }
  return 0;
}

} // namespace demi::cli
