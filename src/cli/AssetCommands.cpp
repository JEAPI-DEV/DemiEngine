#include "cli/AssetCommands.h"
#include "cli/CliArguments.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetPackage.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/ColliderAssetGenerator.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/ModelInspector.h"
#include "demi/assets/SceneBudget3D.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>

namespace demi::cli {
namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

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

std::set<std::string> sectionsAfter(const std::vector<std::string> &args) {
  std::set<std::string> sections;
  const std::string value = valueAfter(args, "--section");
  std::size_t start = 0;
  while (start < value.size()) {
    const std::size_t end = value.find(',', start);
    sections.insert(value.substr(start, end - start));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return sections;
}

nlohmann::json diagnosticsJson(const Diagnostics &diagnostics) {
  nlohmann::json result = nlohmann::json::array();
  for (const Diagnostic &diagnostic : diagnostics)
    result.push_back({{"severity", toString(diagnostic.severity)},
                      {"code", diagnostic.code},
                      {"message", diagnostic.message},
                      {"path", diagnostic.path},
                      {"suggestion", diagnostic.suggestion}});
  return result;
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
               "asset://id [--type type] [--importer id] [--license file]\n";
      return ExitUsageError;
    }
    const std::string license = valueAfter(args, "--license");
    std::optional<assets::ModelImportProfile> profile;
    if (!valueAfter(args, "--preset").empty() ||
        !valueAfter(args, "--up").empty() ||
        !valueAfter(args, "--forward").empty() ||
        !valueAfter(args, "--meters-per-unit").empty()) {
      profile = assets::modelImportPreset(valueAfter(args, "--preset").empty()
                                              ? "static_prop"
                                              : valueAfter(args, "--preset"));
      if (!valueAfter(args, "--up").empty())
        profile->sourceUp = valueAfter(args, "--up");
      if (!valueAfter(args, "--forward").empty())
        profile->sourceForward = valueAfter(args, "--forward");
      if (!valueAfter(args, "--meters-per-unit").empty()) {
        const auto scale = floatAfter(args, "--meters-per-unit");
        if (!scale) {
          error << "asset import --meters-per-unit must be a number.\n";
          return ExitUsageError;
        }
        profile->metersPerUnit = *scale;
      }
      Diagnostics diagnostics;
      const auto checked = assets::parseModelImportProfile(
          assets::modelImportProfileJson(*profile), &diagnostics, args[2]);
      if (!checked)
        return printDiagnostics(diagnostics, error);
      profile = *checked;
    }
    const auto result = assets::importAsset(
        {.projectDirectory = projectDirectory(args),
         .source = args[2],
         .id = valueAfter(args, "--id"),
         .type = valueAfter(args, "--type"),
         .importer = valueAfter(args, "--importer"),
         .license = license.empty()
                        ? std::nullopt
                        : std::make_optional(std::filesystem::path(license)),
         .modelProfile = profile});
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
  if (command == "register-generated") {
    if (args.size() < 3 || valueAfter(args, "--id").empty()) {
      error << "Usage: demi asset register-generated <source> --project "
               "<project> --id asset://id [--type type]\n";
      return ExitUsageError;
    }
    const auto result = assets::registerGeneratedAsset(
        {.projectDirectory = projectDirectory(args),
         .source = args[2],
         .id = valueAfter(args, "--id"),
         .type = valueAfter(args, "--type")});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Registered generated asset: " << result.manifestPath.string()
             << '\n';
    return status;
  }
  if (command == "collider") {
    if (args.size() < 3 ||
        (valueAfter(args, "--id").empty() && !hasArg(args, "--recommend"))) {
      error << "Usage: demi asset collider <model.asset.json> --project "
               "<project> [--recommend] [--body static|dynamic|trigger|"
               "character] [--id asset://colliders/id] [--detail 0..1] "
               "[--preview scene.json]\n";
      return ExitUsageError;
    }
    const std::string body = valueAfter(args, "--body").empty()
                                 ? "static"
                                 : valueAfter(args, "--body");
    if (hasArg(args, "--recommend")) {
      Diagnostics diagnostics;
      const auto recommendation =
          assets::recommendCollider(args[2], body, diagnostics);
      if (recommendation) {
        const nlohmann::json document = {
            {"shape", recommendation->shape},
            {"body", recommendation->body},
            {"detail", recommendation->detail},
            {"reason", recommendation->reason},
            {"component", recommendation->component}};
        if (valueAfter(args, "--format") == "json")
          output << nlohmann::json{{"recommendation", document},
                                   {"diagnostics",
                                    diagnosticsJson(diagnostics)}}
                        .dump(2)
                 << '\n';
        else
          output << "recommended: " << recommendation->shape << " ("
                 << recommendation->reason
                 << ")\ncomponent: " << recommendation->component.dump()
                 << '\n';
      }
      const int status = printDiagnostics(diagnostics, error);
      if (status != ExitSuccess || valueAfter(args, "--id").empty())
        return status;
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
         .detail = *detail,
         .body = body,
         .previewPath = valueAfter(args, "--preview")});
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
  if (command == "budget") {
    if (args.size() < 3) {
      error << "Usage: demi asset budget <demi.project.json> "
               "[--platform android|linux] [--format json]\n";
      return ExitUsageError;
    }
    const std::string platform = valueAfter(args, "--platform").empty()
                                     ? "android"
                                     : valueAfter(args, "--platform");
    if (platform != "android" && platform != "linux") {
      error << "asset budget --platform must be android or linux.\n";
      return ExitUsageError;
    }
    const auto report = assets::inspectSceneBudget3D(args[2], platform);
    if (valueAfter(args, "--format") == "json")
      output << nlohmann::json{{"report", report.document},
                               {"diagnostics",
                                diagnosticsJson(report.diagnostics)}}
                    .dump(2)
             << '\n';
    else {
      output
          << "3D budget (" << platform << "): "
          << report.document.value("observed", nlohmann::json::object()).dump()
          << '\n';
      printDiagnosticsText(error, report.diagnostics);
    }
    return hasErrors(report.diagnostics) ? ExitValidationFailure : ExitSuccess;
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
    if (manifest->type == "Model3D") {
      const auto report = assets::inspectModel(
          {.asset = &*manifest, .sections = sectionsAfter(args)});
      if (valueAfter(args, "--format") == "json") {
        output << nlohmann::json{{"report", report.document},
                                 {"diagnostics",
                                  diagnosticsJson(report.diagnostics)}}
                      .dump(2)
               << '\n';
      } else {
        output << "model: " << manifest->id << '\n'
               << "source: " << manifest->sourcePath.string() << '\n';
        if (report.document.contains("bounds"))
          output << "bounds: " << report.document["bounds"].dump() << '\n';
        if (report.document.contains("metrics"))
          output << "metrics: " << report.document["metrics"].dump() << '\n';
        if (report.document.contains("nodes"))
          output << "nodes: " << report.document["nodes"].dump() << '\n';
        if (report.document.contains("materials"))
          output << "materials: " << report.document["materials"].dump()
                 << '\n';
        if (report.document.contains("animations"))
          output << "animations: " << report.document["animations"].dump()
                 << '\n';
        printDiagnosticsText(error, report.diagnostics);
      }
      return hasErrors(report.diagnostics) ? ExitValidationFailure
                                           : ExitSuccess;
    }
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
