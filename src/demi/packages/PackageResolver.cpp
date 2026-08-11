#include "demi/packages/PackageResolver.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace demi::packages {
namespace {

using RequirementMap = std::map<std::string, std::vector<PackageRequirement>>;

bool acceptsAll(const PackageRelease &release,
                const std::vector<PackageRequirement> &requirements,
                const SemanticVersion &engineVersion) {
  return release.manifest.engineVersion.accepts(engineVersion) &&
         std::ranges::all_of(requirements, [&](const auto &requirement) {
           return requirement.constraint.accepts(release.manifest.version);
         });
}

std::string conflictMessage(const std::string &name,
                            const std::vector<PackageRequirement> &requirements) {
  std::ostringstream output;
  output << "No version of " << name << " satisfies";
  for (const auto &requirement : requirements)
    output << " " << requirement.constraint.text() << " (from "
           << requirement.requestedBy << ")";
  output << ".";
  return output.str();
}

bool hasCycle(const std::map<std::string, PackageRelease> &selected,
              std::string &cycle) {
  enum class Mark { Visiting, Visited };
  std::map<std::string, Mark> marks;
  std::vector<std::string> stack;
  const auto visit = [&](const auto &self, const std::string &name) -> bool {
    if (const auto mark = marks.find(name); mark != marks.end()) {
      if (mark->second == Mark::Visited)
        return false;
      const auto begin = std::ranges::find(stack, name);
      std::ostringstream output;
      for (auto current = begin; current != stack.end(); ++current)
        output << (current == begin ? "" : " -> ") << *current;
      output << " -> " << name;
      cycle = output.str();
      return true;
    }
    marks[name] = Mark::Visiting;
    stack.push_back(name);
    const auto release = selected.find(name);
    if (release != selected.end())
      for (const auto &dependency : release->second.manifest.dependencies)
        if (selected.contains(dependency.name) && self(self, dependency.name))
          return true;
    stack.pop_back();
    marks[name] = Mark::Visited;
    return false;
  };
  for (const auto &[name, unused] : selected) {
    (void)unused;
    if (!marks.contains(name) && visit(visit, name))
      return true;
  }
  return false;
}

struct Resolver {
  PackageCatalog &catalog;
  SemanticVersion engineVersion;
  const std::map<std::string, SemanticVersion> &locked;
  std::map<std::string, std::vector<PackageRelease>> releaseCache;
  Diagnostics catalogDiagnostics;
  std::string lastConflict;

  const std::vector<PackageRelease> &releases(const std::string &name) {
    if (!releaseCache.contains(name)) {
      auto values = catalog.releases(name, catalogDiagnostics);
      std::ranges::sort(values, [](const auto &left, const auto &right) {
        return left.manifest.version > right.manifest.version;
      });
      releaseCache.emplace(name, std::move(values));
    }
    return releaseCache.at(name);
  }

  bool solve(RequirementMap requirements,
             std::map<std::string, PackageRelease> &selected) {
    for (const auto &[name, constraints] : requirements) {
      const auto existing = selected.find(name);
      if (existing != selected.end() &&
          !acceptsAll(existing->second, constraints, engineVersion)) {
        lastConflict = conflictMessage(name, constraints);
        return false;
      }
    }

    std::string next;
    std::vector<PackageRelease> candidates;
    for (const auto &[name, constraints] : requirements) {
      if (selected.contains(name))
        continue;
      std::vector<PackageRelease> matching;
      for (const auto &release : releases(name)) {
        if (release.yanked || !acceptsAll(release, constraints, engineVersion))
          continue;
        if (const auto lock = locked.find(name);
            lock != locked.end() && release.manifest.version != lock->second)
          continue;
        matching.push_back(release);
      }
      if (matching.empty()) {
        lastConflict = conflictMessage(name, constraints);
        return false;
      }
      if (next.empty() || matching.size() < candidates.size() ||
          (matching.size() == candidates.size() && name < next)) {
        next = name;
        candidates = std::move(matching);
      }
    }
    if (next.empty())
      return true;

    for (const auto &candidate : candidates) {
      auto branchRequirements = requirements;
      auto branchSelected = selected;
      branchSelected[next] = candidate;
      for (const auto &dependency : candidate.manifest.dependencies)
        branchRequirements[dependency.name].push_back(
            {.name = dependency.name,
             .constraint = dependency.constraint,
             .requestedBy = candidate.manifest.name + "@" +
                            candidate.manifest.version.string()});
      if (solve(std::move(branchRequirements), branchSelected)) {
        selected = std::move(branchSelected);
        return true;
      }
    }
    return false;
  }
};

} // namespace

PackageResolution
resolvePackages(PackageCatalog &catalog,
                const std::vector<PackageRequirement> &requirements,
                const SemanticVersion &engineVersion,
                const std::map<std::string, SemanticVersion> &locked) {
  PackageResolution resolution;
  RequirementMap grouped;
  for (const auto &requirement : requirements)
    grouped[requirement.name].push_back(requirement);
  Resolver resolver{.catalog = catalog,
                    .engineVersion = engineVersion,
                    .locked = locked};
  if (!resolver.solve(std::move(grouped), resolution.selected)) {
    resolution.diagnostics = std::move(resolver.catalogDiagnostics);
    resolution.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "PACKAGE_VERSION_CONFLICT",
         .message = resolver.lastConflict.empty()
                        ? "Package dependency resolution failed."
                        : resolver.lastConflict,
         .suggestion = "Adjust the direct version constraints or update the "
                       "packages that introduce the conflict."});
    resolution.selected.clear();
    return resolution;
  }
  resolution.diagnostics = std::move(resolver.catalogDiagnostics);
  std::string cycle;
  if (hasCycle(resolution.selected, cycle)) {
    resolution.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "PACKAGE_DEPENDENCY_CYCLE",
         .message = "Package dependency cycle: " + cycle,
         .suggestion = "Remove one dependency edge from the package manifests."});
    resolution.selected.clear();
  }
  return resolution;
}

} // namespace demi::packages
