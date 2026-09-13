#include "cli/HudCommands.h"
#include "cli/CliArguments.h"

#include "demi/runtime/ui/HudLayoutReport.h"

#include <algorithm>
#include <cstdio>
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
        {"resolved",
         {{"x", node.resolved.x},
          {"y", node.resolved.y},
          {"width", node.resolved.width},
          {"height", node.resolved.height}}},
    });
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
        << " focusable=" << (node.focusable ? "true" : "false") << " rect="
        << node.resolved.x << "," << node.resolved.y << ","
        << node.resolved.width << "," << node.resolved.height << '\n';
  }
}

} // namespace

int runHudCommand(const std::vector<std::string> &args, std::ostream &out,
                  std::ostream &error) {
  if (args.size() < 2 || args[1] != "inspect") {
    error << "Usage: demi hud inspect <hud.json> [--safe-area l,t,r,b] "
             "[--reveal-hidden] [--format text|json]\n";
    return 2;
  }
  std::string path;
  std::string format = "text";
  std::string safeArea;
  bool revealHidden = false;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--format" && i + 1 < args.size()) {
      format = args[i + 1];
      ++i;
      continue;
    }
    if (args[i] == "--safe-area" && i + 1 < args.size()) {
      safeArea = args[i + 1];
      ++i;
      continue;
    }
    if (args[i] == "--reveal-hidden") {
      revealHidden = true;
      continue;
    }
    if (!args[i].starts_with("--"))
      path = args[i];
  }
  if (path.empty()) {
    error << "hud inspect requires a .hud.json path.\n";
    return 2;
  }
  const auto document = readJson(path, error);
  if (!document)
    return 1;
  runtime::ui::HudLayoutRequest request;
  request.revealHidden = revealHidden;
  if (!safeArea.empty()) {
    const auto parsed = sscanf(safeArea.c_str(), "%f,%f,%f,%f",
                               &request.safeArea.left, &request.safeArea.top,
                               &request.safeArea.right,
                               &request.safeArea.bottom);
    if (parsed != 4) {
      error << "--safe-area must be four comma-separated numbers: "
               "left,top,right,bottom.\n";
      return 2;
    }
  }
  const runtime::ui::HudLayoutReport report =
      runtime::ui::inspectHudLayout(*document, request);
  if (format == "json") {
    out << reportToJson(report).dump(2) << '\n';
  } else {
    printText(report, out);
  }
  return 0;
}

} // namespace demi::cli
