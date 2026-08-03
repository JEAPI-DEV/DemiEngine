#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

// Owns game-authored GPU programs loaded from cook.manifest.json. Reload is
// transactional: the previous working set survives malformed manifests or
// partial GPU creation failures.
class CookedShaderLibrary {
public:
  explicit CookedShaderLibrary(GpuResources &resources);
  ~CookedShaderLibrary();

  CookedShaderLibrary(const CookedShaderLibrary &) = delete;
  CookedShaderLibrary &operator=(const CookedShaderLibrary &) = delete;

  [[nodiscard]] bool load(const AssetRegistry &registry,
                          std::vector<std::string> &diagnostics);
  void clear();
  [[nodiscard]] ProgramHandle find(std::string_view assetId) const;
  [[nodiscard]] std::size_t size() const { return programs_.size(); }

private:
  GpuResources &resources_;
  std::unordered_map<std::string, ProgramHandle> programs_;
};

} // namespace demi::runtime::render
