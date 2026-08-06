#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace demi::cli::project {

struct ProjectTemplate {
  std::string id;
  std::string title;
  std::string defaultName;
  std::filesystem::path directory;
  std::filesystem::path luaStubPath;
  std::vector<std::filesystem::path> files;
};

class ProjectTemplateCatalog {
public:
  explicit ProjectTemplateCatalog(std::filesystem::path root);

  [[nodiscard]] std::vector<ProjectTemplate>
  discover(Diagnostics &diagnostics) const;
  [[nodiscard]] std::optional<ProjectTemplate>
  find(const std::string &id, Diagnostics &diagnostics) const;

private:
  std::filesystem::path root_;
};

struct ScaffoldRequest {
  ProjectTemplate projectTemplate;
  std::filesystem::path destination;
  std::string projectName;
  bool dryRun = false;
};

struct ScaffoldResult {
  Diagnostics diagnostics;
  std::vector<std::filesystem::path> files;
  bool committed = false;
};

class ProjectScaffolder {
public:
  [[nodiscard]] ScaffoldResult create(const ScaffoldRequest &request) const;
};

[[nodiscard]] int runNewCommand(const std::vector<std::string> &args,
                                const std::filesystem::path &templateRoot,
                                std::ostream &out, std::ostream &error);

} // namespace demi::cli::project
