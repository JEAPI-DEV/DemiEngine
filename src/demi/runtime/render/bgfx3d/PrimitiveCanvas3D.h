#pragma once

#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime::render {

struct PrimitiveVertex3D {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  std::uint32_t rgba = 0xffffffffU;
};

struct PrimitiveCanvas3DStatistics {
  std::uint32_t drawCalls = 0;
  std::uint32_t vertices = 0;
  std::uint32_t indices = 0;
  std::uint32_t triangles = 0;
};

// Backend-neutral transient geometry batch for simple 3D shapes and dynamic
// triangle meshes. Model importing and material ownership intentionally live
// above this low-level submission boundary.
class PrimitiveCanvas3D {
public:
  PrimitiveCanvas3D(GpuResources &resources, RenderCommands &commands);
  ~PrimitiveCanvas3D();

  PrimitiveCanvas3D(const PrimitiveCanvas3D &) = delete;
  PrimitiveCanvas3D &operator=(const PrimitiveCanvas3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool begin(const View3DConfig &view, std::string &error);

  [[nodiscard]] bool cube(const WorldTransform3D &transform, Vec3 size,
                          std::uint32_t rgba);
  [[nodiscard]] bool plane(const WorldTransform3D &transform, Vec3 size,
                           std::uint32_t rgba);
  [[nodiscard]] bool sphere(const WorldTransform3D &transform, float diameter,
                            std::uint32_t rgba, std::uint16_t slices = 16,
                            std::uint16_t stacks = 8);
  [[nodiscard]] bool cylinder(const WorldTransform3D &transform, Vec3 size,
                              std::uint32_t rgba, std::uint16_t slices = 16);
  [[nodiscard]] bool triangles(const WorldTransform3D &transform,
                               std::span<const Vec3> vertices,
                               std::uint32_t rgba);

  [[nodiscard]] bool flush(std::string &error);
  [[nodiscard]] const PrimitiveCanvas3DStatistics &statistics() const {
    return statistics_;
  }

private:
  [[nodiscard]] bool append(std::span<const Vec3> vertices,
                            std::span<const std::uint16_t> indices,
                            const WorldTransform3D &transform,
                            std::uint32_t rgba);

  GpuResources &resources_;
  RenderCommands &commands_;
  ProgramHandle program_;
  std::vector<PrimitiveVertex3D> vertices_;
  std::vector<std::uint16_t> indices_;
  std::uint16_t viewId_ = 0;
  bool frameOpen_ = false;
  PrimitiveCanvas3DStatistics statistics_;
};

} // namespace demi::runtime::render
