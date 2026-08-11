#pragma once

#include "demi/packages/PackageArchive.h"
#include "demi/packages/PackageResolver.h"

#include <filesystem>
#include <memory>
#include <string>

namespace demi::packages {

class PackageRegistry : public PackageCatalog {
public:
  [[nodiscard]] virtual bool
  download(const PackageRelease &release,
           const std::filesystem::path &destination,
           Diagnostics &diagnostics) = 0;
  [[nodiscard]] virtual bool
  publish(const std::filesystem::path &archivePath,
          const PackageArchiveInfo &archive, Diagnostics &diagnostics) = 0;
  [[nodiscard]] virtual std::string source() const = 0;
};

[[nodiscard]] std::unique_ptr<PackageRegistry>
makePackageRegistry(const std::string &source, Diagnostics &diagnostics);
[[nodiscard]] std::unique_ptr<PackageRegistry>
makePackageRegistry(const std::string &source, Diagnostics &diagnostics,
                    const std::filesystem::path &baseDirectory);

} // namespace demi::packages
