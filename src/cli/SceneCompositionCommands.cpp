#include "cli/SceneCompositionCommands.h"
#include "cli/CommandLine.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
namespace demi::cli {
namespace {
using Json = nlohmann::json;
std::optional<Json> readJson(const std::string &path, std::ostream &error) {
  std::ifstream input(path);
  if (!input) {
    error << "Failed to read JSON file: " << path << '\n';
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &e) {
    error << "Invalid JSON in " << path << ": " << e.what() << '\n';
    return std::nullopt;
  }
}
void collectDiffs(const Json &a, const Json &b, const std::string &path,
                  std::vector<std::string> &out) {
  if (a.type() != b.type()) {
    out.push_back(path + ": type changed");
    return;
  }
  if (a.is_object()) {
    std::set<std::string> keys;
    for (const auto &[key, value] : a.items()) {
      (void)value;
      keys.insert(key);
    }
    for (const auto &[key, value] : b.items()) {
      (void)value;
      keys.insert(key);
    }
    for (const auto &key : keys) {
      const std::string child = path + "/" + key;
      if (!a.contains(key))
        out.push_back(child + ": added");
      else if (!b.contains(key))
        out.push_back(child + ": removed");
      else
        collectDiffs(a.at(key), b.at(key), child, out);
    }
  } else if (a.is_array()) {
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
      const std::string child = path + "/" + std::to_string(i);
      if (i >= a.size())
        out.push_back(child + ": added");
      else if (i >= b.size())
        out.push_back(child + ": removed");
      else
        collectDiffs(a[i], b[i], child, out);
    }
  } else if (a != b)
    out.push_back(path + ": " + a.dump() + " -> " + b.dump());
}
} // namespace
int runPrefabCommand(const std::vector<std::string> &args, std::ostream &out,
                     std::ostream &error) {
  CLI::App command{"Inspect prefab composition", "demi prefab"};
  command.require_subcommand(1, 1);
  std::string path;
  command.add_subcommand("inspect")->add_option("path", path)->required();
  if (const auto status = parseCommand(command, args, 1, out, error)) {
    return *status;
  }
  const auto result = runtime::composition::inspectPrefab(path);
  if (!result.document) {
    printDiagnosticsText(error, result.diagnostics);
    return 1;
  }
  out << result.document->dump(2) << '\n';
  return 0;
}
int runSceneExpand(const std::vector<std::string> &args, std::ostream &out,
                   std::ostream &error) {
  CLI::App command{"scene expand", "demi scene expand"};
  std::string path;
  command.add_option("path", path)->required();
  if (const auto status = parseCommand(command, args, 2, out, error)) {
    return *status;
  }
  const auto source = readJson(path, error);
  if (!source)
    return 1;
  const auto result = runtime::composition::expandScene(path, *source);
  if (!result.document) {
    printDiagnosticsText(error, result.diagnostics);
    return 1;
  }
  out << result.document->dump(2) << '\n';
  return 0;
}
int runSceneDiff(const std::vector<std::string> &args, std::ostream &out,
                 std::ostream &error) {
  CLI::App command{"scene diff", "demi scene diff"};
  std::string oldPath;
  command.add_option("old", oldPath)->required();
  std::string newPath;
  command.add_option("new", newPath)->required();
  if (const auto status = parseCommand(command, args, 2, out, error)) {
    return *status;
  }
  const auto a = readJson(oldPath, error), b = readJson(newPath, error);
  if (!a || !b)
    return 1;
  const auto expandedA = runtime::composition::expandScene(oldPath, *a);
  const auto expandedB = runtime::composition::expandScene(newPath, *b);
  if (!expandedA.document || !expandedB.document) {
    printDiagnosticsText(error, expandedA.diagnostics);
    printDiagnosticsText(error, expandedB.diagnostics);
    return 1;
  }
  std::vector<std::string> diffs;
  collectDiffs(*expandedA.document, *expandedB.document, "", diffs);
  if (diffs.empty())
    out << "No differences.\n";
  else
    for (const auto &diff : diffs)
      out << diff << '\n';
  return 0;
}
} // namespace demi::cli
