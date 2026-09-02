#pragma once

#include "demi/capabilities/PlatformCapabilities.h"
#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace demi::cli::doctor {

struct DoctorRequest {
  std::filesystem::path projectPath;
  std::string platform = "linux";
  // Optional engine features of the host renderer runtime. When absent, the
  // fully configured default toolchain is assumed.
  std::optional<capabilities::RuntimeFeatures> hostFeatures;
};

struct DoctorEnvironment {
  std::function<bool(const std::string &)> commandAvailable;
  std::function<std::optional<std::string>(const std::string &)> variable;
  std::function<bool(const std::filesystem::path &)> directoryWritable;
};

[[nodiscard]] DoctorEnvironment systemDoctorEnvironment();

class DoctorService {
public:
  explicit DoctorService(
      DoctorEnvironment environment = systemDoctorEnvironment());
  [[nodiscard]] Diagnostics inspect(const DoctorRequest &request) const;

private:
  DoctorEnvironment environment_;
};

[[nodiscard]] int
runDoctorCommand(const std::vector<std::string> &args, std::ostream &out,
                 std::ostream &error,
                 std::optional<capabilities::RuntimeFeatures> hostFeatures =
                     std::nullopt);

} // namespace demi::cli::doctor
