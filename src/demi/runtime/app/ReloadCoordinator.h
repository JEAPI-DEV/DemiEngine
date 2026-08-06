#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/platform/ProjectFileWatcher.h"

#include <filesystem>
#include <functional>

namespace demi::runtime {

struct ReloadCallbacks {
  std::function<bool(std::string &)> reloadScene;
  std::function<void()> cancelSceneReload;
  std::function<bool(std::string &)> reloadAssets;
};

struct ReloadResult {
  std::uint64_t generation = 0;
  bool applied = false;
  bool luaChanged = false;
  Diagnostics diagnostics;
};

class ReloadCoordinator {
public:
  ReloadCoordinator(std::filesystem::path projectFile,
                    ReloadCallbacks callbacks);
  [[nodiscard]] ReloadResult
  process(const platform::ProjectFileChangeBatch &batch);

private:
  std::filesystem::path projectFile_;
  ReloadCallbacks callbacks_;
  std::uint64_t processedGeneration_ = 0;
};

} // namespace demi::runtime
