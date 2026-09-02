#include "cli/build/PackageContentAudit.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>

namespace demi::build {
namespace {

// Roots the packaged runtime reads from the project tree.
constexpr std::array<std::string_view, 5> AllowedDirectories{
    "assets", "certs", "packages", "scenes", "scripts"};
constexpr std::array<std::string_view, 3> AllowedFiles{
    "cook.manifest.json", "demi.packages.lock.json", "demi.project.json"};

[[nodiscard]] bool isAllowedDirectory(const std::string &name) {
  return std::ranges::find(AllowedDirectories, name) !=
         AllowedDirectories.end();
}

[[nodiscard]] bool isAllowedFile(const std::string &name) {
  return std::ranges::find(AllowedFiles, name) != AllowedFiles.end();
}

[[nodiscard]] int countFiles(const std::filesystem::path &directory) {
  int count = 0;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(directory,
                                                              error),
       end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_regular_file(error))
      ++count;
  }
  return count;
}

} // namespace

PackagedContentAudit
auditPackagedProject(const std::filesystem::path &projectRoot) {
  PackagedContentAudit audit;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(projectRoot, error), end;
       !error && iterator != end; iterator.increment(error)) {
    const std::filesystem::path entry = iterator->path();
    const std::string name = entry.filename().string();
    if (iterator->is_directory(error)) {
      if (!isAllowedDirectory(name)) {
        audit.unexpected.push_back(entry);
        continue;
      }
      audit.fileCounts[name] = countFiles(entry);
      continue;
    }
    if (iterator->is_regular_file(error) && isAllowedFile(name)) {
      ++audit.fileCounts[name];
      continue;
    }
    audit.unexpected.push_back(entry);
  }
  return audit;
}

void stripCookCache(const std::filesystem::path &projectRoot) {
  std::error_code error;
  const std::filesystem::path cache = projectRoot / ".cook-cache";
  if (std::filesystem::exists(cache, error))
    std::filesystem::remove_all(cache, error);
}

} // namespace demi::build
