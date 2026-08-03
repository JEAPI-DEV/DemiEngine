#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/render/backend/CookedShaderLibrary.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

struct MaterialParameter {
  UniformHandle uniform;
  std::array<float, 4> value{};
};

struct MaterialBinding {
  ProgramHandle program;
  std::string albedoTexture;
  DrawState state;
  float alphaCutoff = 0.0F;
  std::uint32_t uniformSet = 0;
  std::vector<MaterialParameter> parameters;
  std::vector<DrawUniformValue> uniforms;
};

// Converts serialized, renderer-independent material assets into GPU-facing
// bindings. Shader programs remain owned by CookedShaderLibrary and uniforms
// by this class.
class MaterialLibrary {
public:
  explicit MaterialLibrary(GpuResources &resources);
  ~MaterialLibrary();

  MaterialLibrary(const MaterialLibrary &) = delete;
  MaterialLibrary &operator=(const MaterialLibrary &) = delete;

  [[nodiscard]] bool load(const AssetRegistry &registry,
                          std::vector<std::string> &diagnostics);
  void clear();
  [[nodiscard]] const MaterialBinding *find(std::string_view assetId) const;

private:
  GpuResources &resources_;
  CookedShaderLibrary shaders_;
  std::unordered_map<std::string, MaterialBinding> materials_;
};

} // namespace demi::runtime::render
