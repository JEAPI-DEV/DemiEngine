#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace demi::assets {

struct MaterialRenderState {
  std::string blend = "opaque";
  std::string cull = "back";
  bool depthTest = true;
  bool depthWrite = true;
};

struct MaterialAsset {
  int formatVersion = 1;
  std::string shader = "builtin://lit";
  std::string fallback = "builtin://unlit";
  std::unordered_map<std::string, std::string> textures;
  std::unordered_map<std::string, float> numbers;
  std::unordered_map<std::string, runtime::Color> colors;
  MaterialRenderState renderState;
};

struct ShaderAsset {
  struct Stages {
    std::filesystem::path vertex;
    std::filesystem::path fragment;
  };

  int formatVersion = 1;
  Stages stages;
  std::optional<Stages> androidStages;
  std::optional<Stages> linuxStages;
  std::string androidFallback = "builtin://unlit";
  std::string linuxFallback = "builtin://unlit";

  [[nodiscard]] const Stages &stagesFor(std::string_view platform) const;
  [[nodiscard]] const std::string &
  fallbackFor(std::string_view platform) const;
};

struct RenderTargetAsset {
  int formatVersion = 1;
  int width = 256;
  int height = 256;
  std::string format = "rgba8";
  bool depth = true;
};

[[nodiscard]] std::optional<MaterialAsset>
loadMaterialAsset(const std::filesystem::path &path,
                  Diagnostics *diagnostics = nullptr);
[[nodiscard]] std::optional<ShaderAsset>
loadShaderAsset(const std::filesystem::path &path,
                Diagnostics *diagnostics = nullptr);
[[nodiscard]] std::optional<RenderTargetAsset>
loadRenderTargetAsset(const std::filesystem::path &path,
                      Diagnostics *diagnostics = nullptr);

} // namespace demi::assets
