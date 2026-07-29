#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/RenderAsset.h"

#include <raylib.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

// Owns game-authored GPU shader programs for one renderer. Asset parsing stays
// backend-independent; this class is the raylib-specific loading boundary.
class ShaderResourceLibrary {
public:
  ShaderResourceLibrary() = default;
  ~ShaderResourceLibrary();

  ShaderResourceLibrary(const ShaderResourceLibrary &) = delete;
  ShaderResourceLibrary &operator=(const ShaderResourceLibrary &) = delete;

  void load(const AssetRegistry &registry);
  void clear();

  [[nodiscard]] const Shader *find(const std::string &assetId) const;
  [[nodiscard]] std::string
  builtinFallbackFor(const std::string &assetId) const;
  [[nodiscard]] std::size_t size() const { return shaders_.size(); }

private:
  [[nodiscard]] std::optional<std::string>
  loadShader(const std::string &assetId, std::unordered_set<std::string> &path);

  std::unordered_map<std::string, assets::ShaderAsset> assets_;
  std::unordered_map<std::string, Shader> shaders_;
  std::unordered_map<std::string, std::string> aliases_;
  std::unordered_map<std::string, std::string> builtinFallbacks_;
};

} // namespace demi::runtime
