#include "cli/project/ProjectTemplates.h"
#include "cli/CliArguments.h"

#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace demi::cli::project {
namespace {

using Json = nlohmann::json;

void add(Diagnostics &diagnostics, Severity severity, std::string code,
         std::string message, const std::filesystem::path &path = {},
         std::string suggestion = {}) {
  diagnostics.push_back({.severity = severity,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = std::move(suggestion)});
}

std::optional<Json> readJson(const std::filesystem::path &path,
                             Diagnostics &diagnostics) {
  std::ifstream input(path);
  if (!input) {
    add(diagnostics, Severity::Error, "TEMPLATE_MANIFEST_READ_FAILED",
        "Could not read the project template manifest.", path);
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &error) {
    add(diagnostics, Severity::Error, "TEMPLATE_MANIFEST_INVALID", error.what(),
        path, "Fix template.json before creating a project.");
    return std::nullopt;
  }
}

bool isContained(const std::filesystem::path &root,
                 const std::filesystem::path &candidate) {
  const auto normalizedRoot = std::filesystem::weakly_canonical(root);
  const auto normalizedCandidate = std::filesystem::weakly_canonical(candidate);
  auto rootPart = normalizedRoot.begin();
  auto candidatePart = normalizedCandidate.begin();
  for (; rootPart != normalizedRoot.end(); ++rootPart, ++candidatePart) {
    if (candidatePart == normalizedCandidate.end() ||
        *rootPart != *candidatePart)
      return false;
  }
  return true;
}

bool isSafeRelative(const std::filesystem::path &path) {
  if (path.empty() || path.is_absolute())
    return false;
  return std::ranges::none_of(
      path, [](const auto &part) { return part == ".." || part == "."; });
}

void replaceAll(std::string &text, const std::string &token,
                const std::string &replacement) {
  std::size_t offset = 0;
  while ((offset = text.find(token, offset)) != std::string::npos) {
    text.replace(offset, token.size(), replacement);
    offset += replacement.size();
  }
}

std::string renderTemplate(std::string text, const std::string &projectName,
                           const std::string &templateId) {
  replaceAll(text, "{{PROJECT_NAME_JSON}}", Json(projectName).dump());
  replaceAll(text, "{{TEMPLATE_ID}}", templateId);
  return text;
}

} // namespace

ProjectTemplateCatalog::ProjectTemplateCatalog(std::filesystem::path root)
    : root_(std::move(root)) {}

std::vector<ProjectTemplate>
ProjectTemplateCatalog::discover(Diagnostics &diagnostics) const {
  std::vector<ProjectTemplate> result;
  std::error_code error;
  if (!std::filesystem::is_directory(root_, error)) {
    add(diagnostics, Severity::Error, "TEMPLATE_ROOT_NOT_FOUND",
        "Project template directory was not found.", root_);
    return result;
  }
  for (const auto &entry : std::filesystem::directory_iterator(root_, error)) {
    if (error)
      break;
    if (!entry.is_directory() ||
        entry.path().filename().string().starts_with("_"))
      continue;
    const auto manifestPath = entry.path() / "template.json";
    if (!std::filesystem::is_regular_file(manifestPath))
      continue;
    auto manifest = readJson(manifestPath, diagnostics);
    if (!manifest)
      continue;
    ProjectTemplate projectTemplate;
    projectTemplate.id = manifest->value("id", std::string{});
    projectTemplate.title = manifest->value("title", std::string{});
    projectTemplate.defaultName =
        manifest->value("default_name", projectTemplate.title);
    projectTemplate.directory = entry.path();
    const auto stub = root_.parent_path() / "scripts/stubs/demi.lua";
    if (std::filesystem::is_regular_file(stub))
      projectTemplate.luaStubPath = stub;
    if (manifest->contains("source") && (*manifest)["source"].is_string())
      projectTemplate.directory =
          entry.path() / (*manifest)["source"].get<std::string>();
    if (projectTemplate.id.empty() || projectTemplate.title.empty() ||
        !manifest->contains("files") || !(*manifest)["files"].is_array()) {
      add(diagnostics, Severity::Error, "TEMPLATE_MANIFEST_INVALID",
          "Template manifest requires id, title, and a files array.",
          manifestPath);
      continue;
    }
    bool valid = true;
    for (const Json &file : (*manifest)["files"]) {
      if (!file.is_string()) {
        valid = false;
        break;
      }
      const std::filesystem::path relative = file.get<std::string>();
      const auto source = projectTemplate.directory / relative;
      if (!isSafeRelative(relative) || !isContained(root_, source) ||
          !std::filesystem::is_regular_file(source)) {
        valid = false;
        break;
      }
      projectTemplate.files.push_back(relative);
    }
    if (!valid) {
      add(diagnostics, Severity::Error, "TEMPLATE_FILE_INVALID",
          "Template contains a missing or unsafe file path.", manifestPath);
      continue;
    }
    std::ranges::sort(projectTemplate.files);
    result.push_back(std::move(projectTemplate));
  }
  if (error)
    add(diagnostics, Severity::Error, "TEMPLATE_DISCOVERY_FAILED",
        error.message(), root_);
  std::ranges::sort(result, {}, &ProjectTemplate::id);
  return result;
}

std::optional<ProjectTemplate>
ProjectTemplateCatalog::find(const std::string &id,
                             Diagnostics &diagnostics) const {
  auto templates = discover(diagnostics);
  const auto found = std::ranges::find(templates, id, &ProjectTemplate::id);
  if (found != templates.end())
    return *found;
  if (!hasErrors(diagnostics))
    add(diagnostics, Severity::Error, "TEMPLATE_NOT_FOUND",
        "Unknown project template: " + id, root_,
        "Run `demi new --list` to see available templates.");
  return std::nullopt;
}

ScaffoldResult ProjectScaffolder::create(const ScaffoldRequest &request) const {
  ScaffoldResult result;
  if (request.destination.empty()) {
    add(result.diagnostics, Severity::Error, "SCAFFOLD_DESTINATION_MISSING",
        "A destination directory is required.");
    return result;
  }
  std::error_code error;
  if (std::filesystem::exists(request.destination, error)) {
    add(result.diagnostics, Severity::Error, "SCAFFOLD_DESTINATION_EXISTS",
        "The destination already exists; no files were changed.",
        request.destination,
        "Choose a new directory or remove the existing directory explicitly.");
    return result;
  }
  const auto parent = request.destination.has_parent_path()
                          ? request.destination.parent_path()
                          : std::filesystem::current_path();
  if (!std::filesystem::is_directory(parent, error)) {
    add(result.diagnostics, Severity::Error, "SCAFFOLD_PARENT_NOT_FOUND",
        "The destination parent directory does not exist.", parent);
    return result;
  }
  for (const auto &relative : request.projectTemplate.files)
    result.files.push_back(request.destination / relative);
  if (!request.projectTemplate.luaStubPath.empty())
    result.files.push_back(request.destination / ".demi/lua/demi.lua");
  if (request.dryRun)
    return result;

  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto staging =
      parent / (".demi-new-" + request.destination.filename().string() + "-" +
                std::to_string(stamp));
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{staging};

  std::filesystem::create_directories(staging, error);
  if (error) {
    add(result.diagnostics, Severity::Error, "SCAFFOLD_STAGE_CREATE_FAILED",
        error.message(), staging);
    return result;
  }
  for (const auto &relative : request.projectTemplate.files) {
    const auto source = request.projectTemplate.directory / relative;
    const auto target = staging / relative;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
      add(result.diagnostics, Severity::Error, "SCAFFOLD_WRITE_FAILED",
          error.message(), target);
      return result;
    }
    std::ifstream input(source, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    std::ofstream output(target, std::ios::binary);
    output << renderTemplate(contents.str(), request.projectName,
                             request.projectTemplate.id);
    if (!input || !output) {
      add(result.diagnostics, Severity::Error, "SCAFFOLD_WRITE_FAILED",
          "Failed to copy a template file.", target);
      return result;
    }
  }
  if (!request.projectTemplate.luaStubPath.empty()) {
    const auto target = staging / ".demi/lua/demi.lua";
    std::filesystem::create_directories(target.parent_path(), error);
    std::filesystem::copy_file(request.projectTemplate.luaStubPath, target,
                               std::filesystem::copy_options::none, error);
    if (error) {
      add(result.diagnostics, Severity::Error, "SCAFFOLD_WRITE_FAILED",
          error.message(), target);
      return result;
    }
  }

  const ValidationSummary validation = validatePath(staging);
  result.diagnostics.insert(result.diagnostics.end(),
                            validation.diagnostics.begin(),
                            validation.diagnostics.end());
  if (hasErrors(result.diagnostics))
    return result;

  std::filesystem::rename(staging, request.destination, error);
  if (error) {
    add(result.diagnostics, Severity::Error, "SCAFFOLD_COMMIT_FAILED",
        error.message(), request.destination,
        "No destination files were committed; check parent permissions.");
    return result;
  }
  cleanup.path.clear();
  result.committed = true;
  return result;
}

int runNewCommand(const std::vector<std::string> &args,
                  const std::filesystem::path &templateRoot, std::ostream &out,
                  std::ostream &error) {
  ProjectTemplateCatalog catalog(templateRoot);
  Diagnostics diagnostics;
  if (args.size() == 2 && args[1] == "--list") {
    for (const auto &item : catalog.discover(diagnostics))
      out << item.id << "\t" << item.title << '\n';
    if (!diagnostics.empty())
      printDiagnosticsText(error, diagnostics);
    return hasErrors(diagnostics) ? 1 : 0;
  }
  if (args.size() < 2 || args[1].starts_with("--")) {
    error << "new requires: demi new <directory> --template <id> "
             "[--name <name>] [--dry-run].\n";
    return 2;
  }
  const std::string templateId = valueAfter(args, "--template");
  if (templateId.empty()) {
    error << "new requires --template <id>.\n";
    return 2;
  }
  auto item = catalog.find(templateId, diagnostics);
  if (!item) {
    printDiagnosticsText(error, diagnostics);
    return 1;
  }
  std::string name = valueAfter(args, "--name");
  if (name.empty())
    name = item->defaultName;
  ScaffoldResult result =
      ProjectScaffolder{}.create({.projectTemplate = *item,
                                  .destination = args[1],
                                  .projectName = name,
                                  .dryRun = hasArg(args, "--dry-run")});
  if (hasErrors(result.diagnostics)) {
    printDiagnosticsText(error, result.diagnostics);
    return 1;
  }
  if (hasArg(args, "--dry-run")) {
    out << "Would create " << result.files.size() << " file(s):\n";
    for (const auto &file : result.files)
      out << "  " << file.string() << '\n';
  } else {
    out << "Created " << name << " from template " << templateId << " at "
        << std::filesystem::path(args[1]).string() << '\n';
  }
  return 0;
}

} // namespace demi::cli::project
