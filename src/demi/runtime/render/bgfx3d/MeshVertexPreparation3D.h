#pragma once

#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

#include <vector>

namespace demi::runtime::render {

// CPU-only, thread-safe preparation. No graphics calls or shared profiler
// state. The caller supplies explicit triangle indices and owns the resulting
// vertices.
[[nodiscard]] bool prepareMeshVertices3D(
    std::span<const Vec3> positions, std::span<const Vec2> uvs,
    std::span<const std::uint32_t> indices, std::uint32_t rgba,
    std::span<const Vec3> normals, std::span<const std::uint32_t> colors,
    std::vector<GpuMeshVertex3D> &vertices, std::string &error);

} // namespace demi::runtime::render
