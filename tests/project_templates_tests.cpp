#include "cli/CliArguments.h"
#include "cli/project/ProjectDiscovery.h"
#include "cli/project/ProjectTemplates.h"

#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

void write(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
  assert(output.good());
}

bool hasCode(const demi::Diagnostics &diagnostics, const std::string &code) {
  for (const auto &diagnostic : diagnostics)
    if (diagnostic.code == code)
      return true;
  return false;
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  using namespace demi::cli::project;
  const std::vector<std::string> arguments{"demi", "--project",
                                           "fixture.project.json", "--watch"};
  assert(demi::cli::hasArg(arguments, "--watch"));
  assert(!demi::cli::hasArg(arguments, "--missing"));
  assert(demi::cli::valueAfter(arguments, "--project") ==
         "fixture.project.json");
  assert(demi::cli::valueAfter(arguments, "--watch").empty());

  const fs::path sourceRoot = DEMI_SOURCE_DIR;
  demi::Diagnostics catalogDiagnostics;
  ProjectTemplateCatalog catalog(sourceRoot / "templates");
  const auto templates = catalog.discover(catalogDiagnostics);
  assert(!demi::hasErrors(catalogDiagnostics));
  assert(templates.size() == 7);

  const auto root = fs::temp_directory_path() / "demi_project_templates_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);

  const fs::path discoveredProject = root / "discovery" / "demi.project.json";
  write(discoveredProject, "{}");
  fs::create_directories(root / "discovery" / "scenes" / "nested");
  assert(demi::cli::findProjectFile(root / "discovery" / "scenes" / "nested") ==
         discoveredProject);
  assert(demi::cli::projectFileFromArgs(
             {"dev"}, root / "discovery" / "scenes") == discoveredProject);
  assert(demi::cli::projectFileFromArgs(
             {"dev", "--project", (root / "discovery").string()}, root) ==
         discoveredProject);

  auto item = catalog.find("visual-novel", catalogDiagnostics);
  assert(item);
  const fs::path destination = root / "quoted-name";
  const auto result =
      ProjectScaffolder{}.create({.projectTemplate = *item,
                                  .destination = destination,
                                  .projectName = "A \"Good\" Story"});
  assert(result.committed);
  assert(!demi::hasErrors(result.diagnostics));
  assert(!demi::hasErrors(demi::validatePath(destination).diagnostics));

  std::ifstream projectInput(destination / "demi.project.json");
  const nlohmann::json project = nlohmann::json::parse(projectInput);
  assert(project["name"] == "A \"Good\" Story");
  assert(project["main_scene"] == "scene://visual-novel/main");

  const auto exists =
      ProjectScaffolder{}.create({.projectTemplate = *item,
                                  .destination = destination,
                                  .projectName = "Must Not Replace"});
  assert(!exists.committed);
  assert(hasCode(exists.diagnostics, "SCAFFOLD_DESTINATION_EXISTS"));
  std::ifstream unchangedInput(destination / "demi.project.json");
  assert(nlohmann::json::parse(unchangedInput)["name"] == "A \"Good\" Story");

  const fs::path dryDestination = root / "dry";
  const auto dry = ProjectScaffolder{}.create({.projectTemplate = *item,
                                               .destination = dryDestination,
                                               .projectName = "Dry Run",
                                               .dryRun = true});
  assert(!dry.committed);
  assert(dry.files.size() == 6);
  assert(!fs::exists(dryDestination));

  const fs::path brokenRoot = root / "broken_catalog";
  write(brokenRoot / "bad/template.json",
        R"({"id":"bad","title":"Bad","files":["../escape"]})");
  demi::Diagnostics brokenDiagnostics;
  assert(
      ProjectTemplateCatalog(brokenRoot).discover(brokenDiagnostics).empty());
  assert(hasCode(brokenDiagnostics, "TEMPLATE_FILE_INVALID"));

  write(brokenRoot / "escape", "must not be copied");
  write(brokenRoot / "traversal/template.json",
        R"({"id":"traversal","title":"Traversal","files":["../escape"]})");
  brokenDiagnostics.clear();
  assert(
      ProjectTemplateCatalog(brokenRoot).discover(brokenDiagnostics).empty());
  assert(hasCode(brokenDiagnostics, "TEMPLATE_FILE_INVALID"));

  const fs::path invalidSource = root / "invalid_source";
  write(invalidSource / "demi.project.json", "{}");
  ProjectTemplate invalid{.id = "invalid",
                          .title = "Invalid",
                          .defaultName = "Invalid",
                          .directory = invalidSource,
                          .files = {"demi.project.json"}};
  const fs::path invalidDestination = root / "invalid_destination";
  const auto invalidResult =
      ProjectScaffolder{}.create({.projectTemplate = invalid,
                                  .destination = invalidDestination,
                                  .projectName = "Invalid"});
  assert(!invalidResult.committed);
  assert(demi::hasErrors(invalidResult.diagnostics));
  assert(!fs::exists(invalidDestination));

  for (const auto &projectTemplate : templates) {
    const fs::path generated = root / projectTemplate.id;
    const auto generatedResult =
        ProjectScaffolder{}.create({.projectTemplate = projectTemplate,
                                    .destination = generated,
                                    .projectName = projectTemplate.title});
    assert(generatedResult.committed);
    assert(!demi::hasErrors(demi::validatePath(generated).diagnostics));
  }

  fs::remove_all(root, ignored);
}
