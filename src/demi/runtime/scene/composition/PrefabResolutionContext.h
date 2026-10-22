#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime::composition {

// Verified package source inventory for one preparation operation. It owns no
// parsed prefabs or expanded documents and never survives the caller's batch.
// Calls remain confined to that preparation owner/thread.
class PrefabResolutionContext {
public:
  PrefabResolutionContext();
  explicit PrefabResolutionContext(const std::filesystem::path &project);
  explicit PrefabResolutionContext(const AssetRegistry &verifiedRegistry);
  ~PrefabResolutionContext();
  PrefabResolutionContext(const PrefabResolutionContext &) = delete;
  PrefabResolutionContext &operator=(const PrefabResolutionContext &) = delete;

  [[nodiscard]] const std::vector<std::filesystem::path> *
  packageFiles(const std::filesystem::path &project);
  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] std::size_t packageVerificationCount() const noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace demi::runtime::composition
