#include "cli/CapabilityCommands.h"
#include "cli/CommandLine.h"

#include "demi/capabilities/CapabilityManifest.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/filesystem/AtomicTextFile.h"
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

int exportManifest(const std::string &outputValue, std::ostream &out,
                   std::ostream &error) {
  const auto manifest = currentManifest(error);
  if (!manifest.has_value()) {
    return Failure;
  }
  std::string content;
  try {
    content = manifest->dump(2) + '\n';
  } catch (const std::exception &failure) {
    error << "Failed to encode capability manifest: " << failure.what() << '\n';
    return Failure;
  }
  if (outputValue.empty()) {
    out << content;
    return Success;
  }

  const std::filesystem::path outputPath = outputValue;
  std::error_code filesystemError;
  if (!demi::atomicWriteText(outputPath, content, filesystemError)) {
    error << "Failed to write capability manifest: " << outputPath.string()
          << ": " << filesystemError.message() << '\n';
    return Failure;
  }
  out << "Wrote capability manifest: " << outputPath.string() << '\n';
  return Success;
}

int checkCompatibility(const std::string &baselineValue,
                       const std::string &format,
                       const CapabilityCommandContext &context,
                       std::ostream &out, std::ostream &error) {
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
  if (format == "json") {
    printDiagnosticsJson(out, diagnostics);
  } else {
    printDiagnosticsText(out, diagnostics);
  }
  return hasErrors(diagnostics) ? Failure : Success;
}

int verifyGates(const std::string &manifestValue,
                const CapabilityCommandContext &context, std::ostream &out,
                std::ostream &error) {
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
  CLI::App command{"Inspect the public capability contract",
                   "demi capabilities"};
  command.require_subcommand(1, 1);
  std::string path;
  std::string format = "text";
  int result = UsageError;
  auto *exportCommand =
      command.add_subcommand("export", "Export the current manifest");
  exportCommand->add_option("--output", path);
  exportCommand->callback([&] { result = exportManifest(path, out, error); });
  auto *check =
      command.add_subcommand("check", "Compare against the public baseline");
  check->add_option("--baseline", path);
  check->add_option("--format", format)->check(CLI::IsMember({"text", "json"}));
  check->callback(
      [&] { result = checkCompatibility(path, format, context, out, error); });
  auto *verify =
      command.add_subcommand("verify-gates", "Verify qualification gates");
  verify->add_option("--manifest", path);
  verify->callback([&] { result = verifyGates(path, context, out, error); });
  if (const auto status = parseCommand(command, args, 1, out, error)) {
    return *status;
  }
  return result;
}

} // namespace demi::cli
