#pragma once
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include <map>
#include <vector>

namespace demi::runtime::render {
// Owns per-camera shadow surfaces and attaches shadow sampling data to mesh
// submissions. Geometry preparation remains in the shared mesh renderer.
class DirectionalShadow3D final : public RenderCommands {
public:
  DirectionalShadow3D(GpuResources &resources, RenderCommands &commands)
      : resources_(resources), commands_(commands) {}
  ~DirectionalShadow3D() override { shutdown(); }
  bool initialize(std::string &error);
  void shutdown();
  void clearTargets();
  bool prepare(const SceneLighting3D &, const BgfxCameraFrame3D &,
               std::string &error);
  const std::optional<BgfxCameraFrame3D> &depthFrame() const { return frame_; }
  void receive();
  bool configureView2D(const View2DConfig &v, std::string &e) override {
    return commands_.configureView2D(v, e);
  }
  bool configureView3D(const View3DConfig &v, std::string &e) override {
    return commands_.configureView3D(v, e);
  }
  bool submit(const TransientDraw &v, std::string &e) override {
    return commands_.submit(v, e);
  }
  bool submit(const BufferedDraw &, std::string &) override;
  bool submit(const InstancedBufferedDraw &, std::string &) override;

private:
  template <class Draw> bool submitMesh(Draw, std::string &);
  GpuResources &resources_;
  RenderCommands &commands_;
  struct Target {
    RenderTargetHandles handles;
    int resolution = 0;
  };
  std::map<std::string, Target> targets_;
  TextureHandle fallback_, sampled_;
  SamplerHandle sampler_;
  std::array<UniformHandle, 4> uniforms_{};
  std::array<std::array<float, 4>, 4> values_{};
  std::optional<BgfxCameraFrame3D> frame_;
  std::vector<DrawUniformValue> drawUniforms_;
  std::vector<DrawTextureBinding> drawTextures_;
};
} // namespace demi::runtime::render
