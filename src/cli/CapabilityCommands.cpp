#include "cli/CapabilityCommands.h"

#include "demi/capabilities/CapabilityManifest.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <string>

namespace demi::cli {
namespace {

using Json = nlohmann::json;

constexpr int Success = 0;
constexpr int Failure = 1;
constexpr int UsageError = 2;

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index) {
    if (args[index] == key) {
      return args[index + 1];
    }
  }
  return {};
}

std::optional<Json> readJson(const std::filesystem::path &path,
                             std::ostream &error) {
  std::ifstream input(path);
  if (!input) {
    error << "Failed to read JSON: " << path.string() << '\n';
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::exception &exception) {
    error << "Invalid JSON in " << path.string() << ": " << exception.what()
          << '\n';
    return std::nullopt;
  }
}

std::optional<Json> currentManifest(std::ostream &error) {
  runtime::World world;
  runtime::InputState input;
  runtime::LuaScriptHost host;
  std::string luaError;
  if (!host.initialize(world, input, nullptr, luaError)) {
    error << "Failed to inspect Lua bindings: " << luaError << '\n';
    return std::nullopt;
  }
  return capabilities::buildManifest(host.publicLuaApi());
}

int exportManifest(const std::vector<std::string> &args,
                   std::ostream &out, std::ostream &error) {
  const auto manifest = currentManifest(error);
  if (!manifest.has_value()) {
    return Failure;
  }
  const std::string outputValue = valueAfter(args, "--output");
  if (outputValue.empty()) {
    out << manifest->dump(2) << '\n';
    return Success;
  }

  const std::filesystem::path outputPath = outputValue;
  std::error_code filesystemError;
  if (!outputPath.parent_path().empty()) {
    std::filesystem::create_directories(outputPath.parent_path(),
                                        filesystemError);
  }
  if (filesystemError) {
    error << "Failed to create capability output directory: "
          << outputPath.parent_path().string() << '\n';
    return Failure;
  }
  std::ofstream output(outputPath);
  if (!output) {
    error << "Failed to write capability manifest: " << outputPath.string()
          << '\n';
    return Failure;
  }
  output << manifest->dump(2) << '\n';
  out << "Wrote capability manifest: " << outputPath.string() << '\n';
  return Success;
}

int checkCompatibility(const std::vector<std::string> &args,
                       const CapabilityCommandContext &context,
                       std::ostream &out, std::ostream &error) {
  const std::string baselineValue = valueAfter(args, "--baseline");
  const std::filesystem::path baselinePath =
      baselineValue.empty()
          ? context.sourceRoot / "capabilities" / "public_api.baseline.json"
          : std::filesystem::path(baselineValue);
  const auto baseline = readJson(baselinePath, error);
  const auto current = currentManifest(error);
  if (!baseline.has_value() || !current.has_value()) {
    return Failure;
  }

  const Diagnostics diagnostics =
      capabilities::comparePublicApi(*baseline, *current);
  if (valueAfter(args, "--format") == "json") {
    printDiagnosticsJson(out, diagnostics);
  } else {
    printDiagnosticsText(out, diagnostics);
  }
  return hasErrors(diagnostics) ? Failure : Success;
}

int verifyGates(const std::vector<std::string> &args,
                const CapabilityCommandContext &context, std::ostream &out,
                std::ostream &error) {
  const std::string manifestValue = valueAfter(args, "--manifest");
  const std::filesystem::path manifestPath =
      manifestValue.empty()
          ? context.sourceRoot / "capabilities" / "reference_gates.json"
          : std::filesystem::path(manifestValue);
  const auto document = readJson(manifestPath, error);
  if (!document.has_value()) {
    return Failure;
  }
  const Diagnostics diagnostics =
      capabilities::verifyReferenceGates(context.sourceRoot, *document);
  printDiagnosticsText(out, diagnostics);
  if (!hasErrors(diagnostics)) {
    out << "Reference capability gates verified.\n";
  }
  return hasErrors(diagnostics) ? Failure : Success;
}

} // namespace

int runCapabilityCommand(const std::vector<std::string> &args,
                         const CapabilityCommandContext &context,
                         std::ostream &out, std::ostream &error) {
  if (args.size() < 2) {
    error << "capabilities requires export, check, or verify-gates.\n";
    return UsageError;
  }
  if (args[1] == "export") {
    return exportManifest(args, out, error);
  }
  if (args[1] == "check") {
    return checkCompatibility(args, context, out, error);
  }
  if (args[1] == "verify-gates") {
    return verifyGates(args, context, out, error);
  }
  error << "Unknown capabilities subcommand: " << args[1] << '\n';
  return UsageError;
}

} // namespace demi::cli
