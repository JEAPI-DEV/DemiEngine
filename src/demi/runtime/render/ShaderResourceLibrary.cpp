#include "demi/runtime/render/ShaderResourceLibrary.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <rlgl.h>

#include <iostream>

namespace demi::runtime {
namespace {

constexpr std::string_view currentShaderPlatform() {
#if defined(__ANDROID__)
  return "android";
#else
  return "linux";
#endif
}

} // namespace

ShaderResourceLibrary::~ShaderResourceLibrary() { clear(); }

void ShaderResourceLibrary::clear() {
  for (auto &[id, shader] : shaders_) {
    (void)id;
    UnloadShader(shader);
  }
  shaders_.clear();
  aliases_.clear();
  builtinFallbacks_.clear();
  assets_.clear();
}

void ShaderResourceLibrary::load(const AssetRegistry &registry) {
  ProfileScope scope("ShaderResourceLibrary.load");
  clear();
  for (const AssetManifest &manifest : registry.assets) {
    if (manifest.type != "Shader")
      continue;
    if (auto shader = assets::loadShaderAsset(manifest.sourcePath))
      assets_.emplace(manifest.id, std::move(*shader));
  }
  for (const AssetManifest &manifest : registry.assets) {
    if (manifest.type != "Shader")
      continue;
    std::unordered_set<std::string> path;
    (void)loadShader(manifest.id, path);
  }
}

std::optional<std::string> ShaderResourceLibrary::loadShader(
    const std::string &assetId, std::unordered_set<std::string> &path) {
  if (shaders_.contains(assetId))
    return assetId;
  if (const auto alias = aliases_.find(assetId); alias != aliases_.end())
    return alias->second;
  const auto asset = assets_.find(assetId);
  if (asset == assets_.end() || !path.insert(assetId).second)
    return std::nullopt;

  const assets::ShaderAsset::Stages &stages =
      asset->second.stagesFor(currentShaderPlatform());
  Shader shader = LoadShader(stages.vertex.string().c_str(),
                             stages.fragment.string().c_str());
  const unsigned int defaultShaderId = rlGetShaderIdDefault();
  if (shader.id != 0 && shader.id != defaultShaderId) {
    shaders_.emplace(assetId, shader);
    path.erase(assetId);
    return assetId;
  }

  const std::string &fallback =
      asset->second.fallbackFor(currentShaderPlatform());
  if (fallback.starts_with("asset://")) {
    if (const auto resolved = loadShader(fallback, path)) {
      aliases_[assetId] = *resolved;
      path.erase(assetId);
      std::cerr << "Shader " << assetId << " failed to compile on "
                << currentShaderPlatform() << "; using " << *resolved
                << ".\n";
      return resolved;
    }
    if (const auto inherited = builtinFallbacks_.find(fallback);
        inherited != builtinFallbacks_.end())
      builtinFallbacks_[assetId] = inherited->second;
  } else if (fallback.starts_with("builtin://")) {
    builtinFallbacks_[assetId] = fallback;
  }
  path.erase(assetId);
  std::cerr << "Shader " << assetId << " failed to compile on "
            << currentShaderPlatform() << "; using " << fallback << ".\n";
  return std::nullopt;
}

const Shader *
ShaderResourceLibrary::find(const std::string &assetId) const {
  std::string resolved = assetId;
  if (const auto alias = aliases_.find(resolved); alias != aliases_.end())
    resolved = alias->second;
  const auto found = shaders_.find(resolved);
  return found == shaders_.end() ? nullptr : &found->second;
}

std::string ShaderResourceLibrary::builtinFallbackFor(
    const std::string &assetId) const {
  const auto found = builtinFallbacks_.find(assetId);
  return found == builtinFallbacks_.end() ? std::string{} : found->second;
}

} // namespace demi::runtime
