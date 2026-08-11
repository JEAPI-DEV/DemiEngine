#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/packages/PackageManifest.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace demi::packages {

struct PackageRelease {
  PackageManifest manifest;
  std::string manifestHash;
  std::string archiveHash;
  std::string archiveUri;
  bool yanked = false;
};

class PackageCatalog {
public:
  virtual ~PackageCatalog() = default;
  [[nodiscard]] virtual std::vector<PackageRelease>
  releases(const std::string &name, Diagnostics &diagnostics) = 0;
};

struct PackageRequirement {
  std::string name;
  VersionConstraint constraint;
  std::string requestedBy;
};

struct PackageResolution {
  std::map<std::string, PackageRelease> selected;
  Diagnostics diagnostics;
};

[[nodiscard]] PackageResolution
resolvePackages(PackageCatalog &catalog,
                const std::vector<PackageRequirement> &requirements,
                const SemanticVersion &engineVersion,
                const std::map<std::string, SemanticVersion> &locked = {});

} // namespace demi::packages
