#pragma once

#include "demi/runtime/render/bgfx3d/WaterSurfaceRenderer3D.h"
#include <map>

namespace demi::runtime::render {

// Per-camera snapshots stay immutable until all consumers have submitted.
// MRT writes analytic eye depth beside scene color when supported. Legacy
// targets use a separate sampling pass only for explicit capability limits.
class OpaqueSceneCapture3D {
public:
  enum class PathReason {
    AnalyticMrt = 0,
    InsufficientAttachments = 1,
    UnsupportedLinearDepthTarget = 2,
    UnsupportedMultisampleTarget = 3,
    UnsupportedMultisampleSampling = 4,
    UnsupportedCustomDepthOutput = 5,
    UnsupportedMultisampleCountQuery = 6,
    UnsupportedPreservedAuthoredColor = 7
  };

  struct Target {
    RenderTargetHandles depth;
    TextureHandle color;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool used = false;
    int requestedSamples = 1;
    PathReason pathReason = PathReason::InsufficientAttachments;
    RenderTargetHandles scene;
    FrameBufferHandle opaqueFrameBuffer;
    TextureHandle analyticDepth;
    [[nodiscard]] bool usesMrt() const {
      return pathReason == PathReason::AnalyticMrt;
    }
    WaterSceneCapture3D sample(bool originBottomLeft) const;
  };

  explicit OpaqueSceneCapture3D(GpuResources &resources)
      : resources_(resources) {}
  ~OpaqueSceneCapture3D();
  OpaqueSceneCapture3D(const OpaqueSceneCapture3D &) = delete;
  OpaqueSceneCapture3D &operator=(const OpaqueSceneCapture3D &) = delete;
  void beginPresentation();
  void endPresentation();
  void clear();
  Target *prepare(std::uint16_t cameraView, std::uint16_t width,
                  std::uint16_t height, std::string &error, int msaaSamples = 1,
                  bool customDepthOutputUnsupported = false,
                  bool preserveAuthoredTargetColor = false);
  [[nodiscard]] static bool
  requiresPreservedAuthoredColor(const Camera3DComponent &camera) {
    return !camera.renderTarget.empty() && camera.clearMode != "color";
  }
  // A fullscreen min-sample resolve replaces the repeated geometry pass.
  // Call after switching away from opaqueFrameBuffer and before optical reads.
  bool resolveDepth(Target &target, RenderCommands &commands,
                    std::uint16_t view, std::string &error);

private:
  void destroy(Target &target);
  bool initializeResolve(std::string &error);
  bool allocateMrt(Target &target, std::uint16_t width, std::uint16_t height,
                   std::string &error);
  bool allocatePrepass(Target &target, std::uint16_t width,
                       std::uint16_t height, std::string &error);
  GpuResources &resources_;
  ProgramHandle resolveProgram_;
  SamplerHandle resolveSampler_;
  UniformHandle resolveSamples_;
  std::map<std::uint16_t, Target> targets_;
};
} // namespace demi::runtime::render
