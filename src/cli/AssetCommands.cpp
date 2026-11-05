#include "cli/AssetCommands.h"
#include "cli/CommandLine.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetPackage.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/ColliderAssetGenerator.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/ModelInspector.h"
#include "demi/assets/ModelLodGenerator.h"
#include "demi/assets/SceneBudget3D.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <utility>

namespace demi::cli {
namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

std::filesystem::path projectDirectory(const std::string &value) {
  if (value.empty())
    return std::filesystem::current_path();
  const std::filesystem::path path(value);
  return std::filesystem::is_directory(path) ? path : path.parent_path();
}

std::filesystem::path
projectDirectoryForManifest(const std::string &requestedProject,
                            const std::filesystem::path &manifestPath) {
  if (!requestedProject.empty())
    return projectDirectory(requestedProject);
  auto directory = std::filesystem::absolute(manifestPath).parent_path();
  while (!directory.empty()) {
    if (std::filesystem::is_regular_file(directory / "demi.project.json"))
      return directory;
    const auto parent = directory.parent_path();
    if (parent == directory)
      break;
    directory = parent;
  }
  return projectDirectory(requestedProject);
}

int printDiagnostics(const Diagnostics &diagnostics, std::ostream &error) {
  if (!diagnostics.empty())
    printDiagnosticsText(error, diagnostics);
  return hasErrors(diagnostics) ? ExitValidationFailure : ExitSuccess;
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
  CLI::App app{"Import, inspect and package assets", "demi asset"};
  app.require_subcommand(1, 1);
  std::string path;
  std::string requestedProject;
  std::string id;
  std::string type;
  std::string importer;
  std::string contentType;
  std::string license;
  std::string preset;
  std::string up;
  std::string forward;
  std::string format = "text";
  std::string body = "static";
  std::string preview;
  std::string outputPath;
  std::string platform = "android";
  std::optional<float> metersPerUnit;
  float detail = 0;
  bool recommend = false;
  bool replace = false;
  bool overwrite = false;
  std::vector<std::string> ids;
  std::set<std::string> sections;
  assets::ModelLodGenerationRequest lodRequest;
  for (const auto *name :
       {"fracture", "import", "reimport", "register-generated", "lod",
        "collider", "import-package", "budget", "inspect", "deps"}) {
    app.add_subcommand(name)->add_option("path", path)->required();
  }
  app.add_subcommand("export");
  for (const auto *name : {"import", "register-generated", "lod", "collider",
                           "export", "import-package", "deps"}) {
    app.get_subcommand(name)->add_option("--project", requestedProject);
  }
  for (const auto *name : {"lod", "collider", "budget", "inspect"}) {
    app.get_subcommand(name)
        ->add_option("--format", format)
        ->check(CLI::IsMember({"text", "json"}));
  }
  for (const auto *name : {"import", "register-generated"}) {
    auto *command = app.get_subcommand(name);
    command->add_option("--id", id)->required();
    command->add_option("--type", type);
  }
  auto *importCommand = app.get_subcommand("import");
  importCommand->add_option("--importer", importer);
  auto *contentTypeOption =
      importCommand->add_option("--content-type", contentType);
  importCommand->add_option("--license", license);
  importCommand->add_option("--preset", preset);
  importCommand->add_option("--up", up);
  importCommand->add_option("--forward", forward);
  importCommand->add_option("--meters-per-unit", metersPerUnit);
  auto *lod = app.get_subcommand("lod");
  lod->add_option("--medium-ratio", lodRequest.mediumRatio);
  lod->add_option("--low-ratio", lodRequest.lowRatio);
  lod->add_option("--max-error", lodRequest.maxError);
  auto *collider = app.get_subcommand("collider");
  auto *identity = collider->add_option_group(
      "Output", "Generate an ID or request a recommendation");
  identity->add_option("--id", id);
  identity->add_flag("--recommend", recommend);
  identity->require_option(1);
  collider->add_option("--body", body)
      ->check(CLI::IsMember({"static", "dynamic", "trigger", "character"}));
  collider->add_option("--detail", detail)->check(CLI::Range(0.0F, 1.0F));
  collider->add_option("--preview", preview);
  collider->add_flag("--replace", replace);
  auto *exportCommand = app.get_subcommand("export");
  exportCommand->add_option("--asset", ids)->required()->expected(1);
  exportCommand->add_option("--output", outputPath)->required();
  app.get_subcommand("import-package")->add_flag("--overwrite", overwrite);
  app.get_subcommand("budget")
      ->add_option("--platform", platform)
      ->check(CLI::IsMember({"android", "linux"}));
  app.get_subcommand("inspect")
      ->add_option("--section", sections)
      ->delimiter(',');
  if (const auto status = parseCommand(app, args, 1, output, error)) {
    return *status;
  }
  const std::string command = app.get_subcommands().front()->get_name();
  if (command == "fracture") {
    const auto result = runtime::composition::bakeFracturePrefab(path);
    if (!result.document)
      return printDiagnostics(result.diagnostics, error);
    std::size_t chunks = 0, bonds = 0, anchors = 0;
    for (const auto &entity : (*result.document)["entities"]) {
      const auto &c = entity["components"];
      if (!c.contains("Destructible3D") || !c.contains("ModelCollider3D") ||
          !c["ModelCollider3D"].contains("inline_geometry"))
        continue;
      const auto &geometry = c["ModelCollider3D"]["inline_geometry"];
      chunks += geometry["parts"].size();
      bonds += geometry["fracture"]["bonds"].size();
      anchors += geometry["fracture"]["anchors"].size();
    }
    output << nlohmann::json{{"prefab", (*result.document)["id"]},
                             {"chunks", chunks},
                             {"bonds", bonds},
                             {"anchors", anchors},
                             {"source_unchanged", true}}
                  .dump(2)
           << '\n';
    return ExitSuccess;
  }
  if (command == "import") {
    std::optional<assets::ModelImportProfile> profile;
    if (!preset.empty() || !up.empty() || !forward.empty() ||
        metersPerUnit.has_value()) {
      profile =
          assets::modelImportPreset(preset.empty() ? "static_prop" : preset);
      if (!up.empty())
        profile->sourceUp = up;
      if (!forward.empty())
        profile->sourceForward = forward;
      if (metersPerUnit.has_value()) {
        profile->metersPerUnit = *metersPerUnit;
      }
      Diagnostics diagnostics;
      const auto checked = assets::parseModelImportProfile(
          assets::modelImportProfileJson(*profile), &diagnostics, path);
      if (!checked)
        return printDiagnostics(diagnostics, error);
      profile = *checked;
    }
    const auto result = assets::importAsset(
        {.projectDirectory = projectDirectory(requestedProject),
         .source = path,
         .id = id,
         .type = type,
         .importer = importer,
         .license = license.empty()
                        ? std::nullopt
                        : std::make_optional(std::filesystem::path(license)),
         .modelProfile = profile,
         .dataContentType = contentTypeOption->count() != 0
                                ? std::make_optional(contentType)
                                : std::nullopt});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Imported asset: " << result.manifestPath.string() << '\n';
    return status;
  }
  if (command == "reimport") {
    const auto diagnostics = assets::reimportAsset(path);
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Reimported asset: " << path << '\n';
    return status;
  }
  if (command == "register-generated") {
    const auto result = assets::registerGeneratedAsset(
        {.projectDirectory = projectDirectory(requestedProject),
         .source = path,
         .id = id,
         .type = type});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Registered generated asset: " << result.manifestPath.string()
             << '\n';
    return status;
  }
  if (command == "lod") {
    lodRequest.projectDirectory =
        projectDirectoryForManifest(requestedProject, path);
    lodRequest.modelManifestPath = path;
    const auto result = assets::generateModelLods(lodRequest);
    if (format == "json") {
      auto variants = nlohmann::json::array();
      for (const auto &variant : result.variants) {
        variants.push_back({{"level", variant.level},
                            {"id", variant.id},
                            {"manifest", variant.manifestPath.string()},
                            {"source_triangles", variant.sourceTriangles},
                            {"triangles", variant.triangles},
                            {"achieved_error", variant.achievedError}});
      }
      output << nlohmann::json{{"source_model", result.sourceModelId},
                               {"recipe", result.recipePath.string()},
                               {"variants", variants},
                               {"diagnostics",
                                diagnosticsJson(result.diagnostics)}}
                    .dump(2)
             << '\n';
    } else {
      for (const auto &variant : result.variants) {
        output << variant.level << ": " << variant.id << " ("
               << variant.sourceTriangles << " -> " << variant.triangles
               << " triangles, error " << variant.achievedError << ")\n";
      }
    }
    return printDiagnostics(result.diagnostics, error);
  }
  if (command == "collider") {
    if (recommend) {
      Diagnostics diagnostics;
      const auto recommendation =
          assets::recommendCollider(path, body, diagnostics);
      if (recommendation) {
        const nlohmann::json document = {
            {"shape", recommendation->shape},
            {"body", recommendation->body},
            {"detail", recommendation->detail},
            {"reason", recommendation->reason},
            {"component", recommendation->component}};
        if (format == "json")
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
      if (status != ExitSuccess || id.empty())
        return status;
    }
    const auto result = assets::generateColliderAsset(
        {.projectDirectory = projectDirectory(requestedProject),
         .modelManifestPath = path,
         .id = id,
         .detail = detail,
         .body = body,
         .previewPath = preview,
         .replaceExisting = replace});
    const int status = printDiagnostics(result.diagnostics, error);
    if (status == ExitSuccess)
      output << "Generated collider asset: " << result.manifestPath.string()
             << '\n';
    return status;
  }
  if (command == "export") {
    const auto diagnostics = assets::exportAssetPackage(
        {.projectDirectory = projectDirectory(requestedProject),
         .outputPath = outputPath,
         .assetIds = ids});
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Exported asset package: " << outputPath << '\n';
    return status;
  }
  if (command == "import-package") {
    std::vector<std::string> conflicts;
    const auto diagnostics = assets::importAssetPackage(
        {.projectDirectory = projectDirectory(requestedProject),
         .packagePath = path,
         .overwrite = overwrite},
        &conflicts);
    for (const auto &conflict : conflicts)
      error << "conflict: " << conflict << '\n';
    const int status = printDiagnostics(diagnostics, error);
    if (status == ExitSuccess)
      output << "Imported asset package: " << path << '\n';
    return status;
  }
  if (command == "budget") {
    const auto report = assets::inspectSceneBudget3D(path, platform);
    if (format == "json")
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
  Diagnostic diagnostic;
  const auto manifest = loadAssetManifest(path, &diagnostic);
  if (!manifest) {
    printDiagnosticsText(error, Diagnostics{diagnostic});
    return ExitValidationFailure;
  }
  if (command == "inspect") {
    if (manifest->type == "Collider3D" &&
        manifest->importer == "collider-shape") {
      std::string issue;
      const auto collider =
          assets::loadColliderShapeAsset(manifest->sourcePath, issue);
      if (!collider) {
        error << issue << '\n';
        return ExitValidationFailure;
      }
      nlohmann::json report{
          {"id", manifest->id},
          {"shape", collider->parts.empty() ? "convex_hull" : "compound"},
          {"points",
           [&] {
             auto count = collider->points.size();
             for (const auto &part : collider->parts)
               count += part.points.size();
             return count;
           }()},
          {"part_ids",
           [&] {
             auto ids = nlohmann::json::array();
             for (const auto &part : collider->parts)
               ids.push_back(part.id);
             return ids;
           }()},
          {"dynamic_supported", true},
          {"bounds",
           {{"minimum", collider->minimum}, {"maximum", collider->maximum}}}};
      if (collider->fracture) {
        auto bonds = nlohmann::json::array();
        for (const auto &bond : collider->fracture->bonds)
          bonds.push_back({{"id", bond.id},
                           {"parts", {bond.firstPart, bond.secondPart}},
                           {"health", bond.health}});
        report["fracture"] = {{"bonds", std::move(bonds)},
                              {"anchors", collider->fracture->anchors}};
      }
      if (format == "json")
        output << nlohmann::json{{"report", report},
                                 {"diagnostics", nlohmann::json::array()}}
                      .dump(2)
               << '\n';
      else
        output << "collider: " << report.dump() << '\n';
      return ExitSuccess;
    }
    if (manifest->type == "Model3D") {
      const auto report =
          assets::inspectModel({.asset = &*manifest, .sections = sections});
      if (format == "json") {
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
  const AssetRegistry registry =
      loadAssetRegistry(projectDirectoryForManifest(requestedProject, path));
  Diagnostics diagnostics;
  for (const auto *dependency :
       assetDependencies(registry, *manifest, &diagnostics))
    output << dependency->id << '\n';
  for (const auto &file : assets::collectAssetFiles(*manifest))
    output << file.string() << '\n';
  return printDiagnostics(diagnostics, error);
}

} // namespace demi::cli
