#pragma once

#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <array>
#include <functional>
#include <vector>

namespace demi::runtime::render {

// Program selection and binding must agree after mesh-level overrides.
// A custom program retains ownership of its own parameters and samplers.
[[nodiscard]] bool usesMeshPbrSurface3D(const MaterialBinding *material,
                                        const MeshRendererComponent &mesh);

// Per-mesh bindings are joined synchronously; field-patch draws never pass
// through this adapter and keep their independent sampler layout.
class MeshPbrBindings3D final : public RenderCommands {
public:
  using TextureResolver =
      std::function<TextureView2D(std::string_view, ImageMipSemantic)>;
  MeshPbrBindings3D(GpuResources &resources, RenderCommands &commands,
                    TextureResolver resolve);
  ~MeshPbrBindings3D() override;
  bool initialize(std::string &error);
  void shutdown();
  bool prepare(const MaterialBinding *material,
               const MeshRendererComponent &mesh, TextureHandle white,
               float terrainTextureScale, std::string &error);
  void reset();
  void setFrameTime(float seconds) { frameTime_ = seconds; }
  std::uint32_t availableInstances(std::uint16_t stride,
                                   std::uint32_t requested) const override;
  bool configureView2D(const View2DConfig &view, std::string &error) override;
  bool configureView3D(const View3DConfig &view, std::string &error) override;
  bool submit(const TransientDraw &draw, std::string &error) override;
  bool submit(const BufferedDraw &draw, std::string &error) override;
  bool submit(const InstancedBufferedDraw &draw, std::string &error) override;

private:
  template <class Draw> bool submitMesh(Draw draw, std::string &error);
  GpuResources &resources_;
  RenderCommands &commands_;
  TextureResolver resolve_;
  bool enabled_ = false;
  UniformHandle windUniform_;
  float frameTime_ = 0;
  std::array<float, 4> windValues_{};
  std::array<SamplerHandle, 6> samplers_{};
  std::array<UniformHandle, 5> uniforms_{};
  std::array<std::array<float, 4>, 5> values_{};
  std::array<DrawTextureBinding, 6> bindings_{};
  std::vector<DrawUniformValue> joinedUniforms_;
  std::vector<DrawTextureBinding> joinedTextures_;
};

} // namespace demi::runtime::render
