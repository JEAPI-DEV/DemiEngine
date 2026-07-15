#include "cli/AssetCommands.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetPackage.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/ColliderAssetGenerator.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>

namespace demi::cli {
namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index)
    if (args[index] == key)
      return args[index + 1];
  return {};
}

std::optional<float> floatAfter(const std::vector<std::string> &args,
                                const std::string &key) {
  const std::string value = valueAfter(args, key);
  if (value.empty())
    return std::nullopt;
  try {
    return std::stof(value);
  } catch (...) {
    return std::nullopt;
  }
}

bool hasArg(const std::vector<std::string> &args, const std::string &key) {
  return std::ranges::find(args, key) != args.end();
}

std::filesystem::path projectDirectory(const std::vector<std::string> &args) {
  const std::string value = valueAfter(args, "--project");
  if (value.empty())
    return std::filesystem::current_path();
  const std::filesystem::path path(value);
  return std::filesystem::is_directory(path) ? path : path.parent_path();
}

std::filesystem::path
projectDirectoryForManifest(const std::vector<std::string> &args,
                            const std::filesystem::path &manifestPath) {
  if (!valueAfter(args, "--project").empty())
    return projectDirectory(args);
  auto directory = std::filesystem::absolute(manifestPath).parent_path();
  while (!directory.empty()) {
    if (std::filesystem::is_regular_file(directory / "demi.project.json"))
      return directory;
    const auto parent = directory.parent_path();
    if (parent == directory)
      break;
    directory = parent;
  }
  return projectDirectory(args);
}

int printDiagnostics(const Diagnostics &diagnostics, std::ostream &error) {
  if (!diagnostics.empty())
    printDiagnosticsText(error, diagnostics);
  return hasErrors(diagnostics) ? ExitValidationFailure : ExitSuccess;
}

std::vector<std::string> selectedAssets(const std::vector<std::string> &args) {
  std::vector<std::string> ids;
  for (std::size_t index = 0; index + 1 < args.size(); ++index)
    if (args[index] == "--asset")
      ids.push_back(args[index + 1]);
  return ids;
}

} // namespace

int runAssetCommand(const std::vector<std::string> &args, std::ostream &output,
                    std::ostream &error) {
  if (args.size() < 2) {
    error << "asset requires a subcommand.\n";
    return ExitUsageError;
  }
  const std::string &command = args[1];
  if (command == "import") {
    if (args.size() < 3 || valueAfter(args, "--id").empty()) {
      error << "Usage: demi asset import <source> --project <project> --id "
               "asset://id [--type type] [--license file]\n";
      return ExitUsageError;
    }
    const std::string license = valueAfter(args, "--license");
    const auto result = assets::importAsset(
        {.projectDirectory = projectDirectory(args),
         .source = args[2],
         .id = valueAfter(args, "--id"),
         .type = valueAfter(args, "--type"),
         .license = license.empty()
                        ? std::nullopt
                        : std::make_optional(std::filesystem::path(license))});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Imported asset: " << result.manifestPath.string() << '\n';
    return status;
  }
  if (command == "reimport") {
    if (args.size() < 3)
      return ExitUsageError;
    const auto diagnostics = assets::reimportAsset(args[2]);
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Reimported asset: " << args[2] << '\n';
    return status;
  }
  if (command == "collider") {
    if (args.size() < 3 || valueAfter(args, "--id").empty()) {
      error << "Usage: demi asset collider <model.asset.json> --project "
               "<project> --id asset://colliders/id [--detail 0..1]\n";
      return ExitUsageError;
    }
    const std::string requestedDetail = valueAfter(args, "--detail");
    const auto detail = requestedDetail.empty() ? std::make_optional(0.0F)
                                                : floatAfter(args, "--detail");
    if (!detail) {
      error << "asset collider --detail must be a number between 0 and 1.\n";
      return ExitUsageError;
    }
    const auto result = assets::generateColliderAsset(
        {.projectDirectory = projectDirectory(args),
         .modelManifestPath = args[2],
         .id = valueAfter(args, "--id"),
         .detail = *detail});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Generated collider asset: " << result.manifestPath.string()
             << '\n';
    return status;
  }
  if (command == "export") {
    const auto ids = selectedAssets(args);
    const std::string outputPath = valueAfter(args, "--output");
    if (ids.empty() || outputPath.empty()) {
      error << "Usage: demi asset export --project <project> --output "
               "<file.demipack> --asset asset://id [--asset ...]\n";
      return ExitUsageError;
    }
    const auto diagnostics =
        assets::exportAssetPackage({.projectDirectory = projectDirectory(args),
                                    .outputPath = outputPath,
                                    .assetIds = ids});
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Exported asset package: " << outputPath << '\n';
    return status;
  }
  if (command == "import-package") {
    if (args.size() < 3) {
      error << "Usage: demi asset import-package <file.demipack> --project "
               "<project> [--overwrite]\n";
      return ExitUsageError;
    }
    std::vector<std::string> conflicts;
    const auto diagnostics =
        assets::importAssetPackage({.projectDirectory = projectDirectory(args),
                                    .packagePath = args[2],
                                    .overwrite = hasArg(args, "--overwrite")},
                                   &conflicts);
    for (const auto &conflict : conflicts)
      error << "conflict: " << conflict << '\n';
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Imported asset package: " << args[2] << '\n';
    return status;
  }
  if (args.size() < 3) {
    error << "asset " << command << " requires a manifest path.\n";
    return ExitUsageError;
  }
  Diagnostic diagnostic;
  const auto manifest = loadAssetManifest(args[2], &diagnostic);
  if (!manifest) {
    printDiagnosticsText(error, Diagnostics{diagnostic});
    return ExitValidationFailure;
  }
  if (command == "inspect") {
    output << "id: " << manifest->id << '\n'
           << "type: " << manifest->type << '\n'
           << "importer: " << manifest->importer << '\n'
           << "importer_version: " << manifest->importerVersion << '\n'
           << "source_hash: " << manifest->sourceHash << '\n'
           << "source: " << manifest->sourcePath.string() << '\n';
    return std::filesystem::exists(manifest->sourcePath)
               ? ExitSuccess
               : ExitValidationFailure;
  }
  if (command == "deps") {
    const AssetRegistry registry =
        loadAssetRegistry(projectDirectoryForManifest(args, args[2]));
    Diagnostics diagnostics;
    for (const auto *dependency :
         assetDependencies(registry, *manifest, &diagnostics))
      output << dependency->id << '\n';
    for (const auto &file : assets::collectAssetFiles(*manifest))
      output << file.string() << '\n';
    return printDiagnostics(diagnostics, error);
  }
  error << "Unknown asset subcommand: " << command << '\n';
  return ExitUsageError;
}

} // namespace demi::cli
