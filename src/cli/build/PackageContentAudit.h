#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace demi::build {

// Package-content audit for a cooked project tree that is about to be
// packaged. Only the roots the packaged runtime reads are allowed; anything
// else (build caches, editor state, user saves, tests) is reported so
// packaging can reject or strip it before release.
struct PackagedContentAudit {
  // File count per allowed root ("assets", "scenes", "scripts", ...).
  std::map<std::string, int> fileCounts;
  // Entries outside the packaged-content allowlist.
  std::vector<std::filesystem::path> unexpected;
};

[[nodiscard]] PackagedContentAudit
auditPackagedProject(const std::filesystem::path &projectRoot);

// Removes the cook cache from a cooked project tree. The cache is a build
// accelerator, never packaged content.
void stripCookCache(const std::filesystem::path &projectRoot);

} // namespace demi::build
