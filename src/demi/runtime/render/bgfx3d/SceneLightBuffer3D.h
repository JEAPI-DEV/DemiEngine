#pragma once
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include <map>
#include <vector>

namespace demi::runtime::render {
// Owns per-view light textures. Separate textures are required because bgfx
// uploads happen before submissions: two cameras must not overwrite each other.
class SceneLightBuffer3D final : public RenderCommands {
public:
  SceneLightBuffer3D(GpuResources &resources, RenderCommands &commands)
      : resources_(resources), commands_(commands) {}
  ~SceneLightBuffer3D() override { shutdown(); }
  bool prepare(std::uint16_t view, const SceneLighting3D &lighting,
               std::string &error);
  void shutdown();
  bool configureView2D(const View2DConfig &v, std::string &e) override {
    return commands_.configureView2D(v, e);
  }
  bool configureView3D(const View3DConfig &v, std::string &e) override {
    return commands_.configureView3D(v, e);
  }
  bool submit(const TransientDraw &v, std::string &e) override {
    return commands_.submit(v, e);
  }
  bool submit(const BufferedDraw &v, std::string &e) override;
  bool submit(const InstancedBufferedDraw &v, std::string &e) override;

private:
  template <class Draw> bool submitMesh(Draw draw, std::string &error);
  struct ViewLights {
    TextureHandle texture;
    std::uint16_t width = 0, height = 0;
    std::array<float, 4> parameters{};
    std::vector<float> source;
  };
  GpuResources &resources_;
  RenderCommands &commands_;
  TextureHandle fallback_;
  const std::array<float, 4> noLights_{0, 1, 1, 0};
  SamplerHandle sampler_;
  UniformHandle parameters_;
  std::map<std::uint16_t, ViewLights> views_;
  std::vector<DrawUniformValue> uniforms_;
  std::vector<DrawTextureBinding> textures_;
};
} // namespace demi::runtime::render
