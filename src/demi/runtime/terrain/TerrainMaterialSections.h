#pragma once

#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

#include <cstddef>
#include <span>

namespace demi::runtime {
struct HeightField;
namespace terrain_detail {
// Groups render indices by surface role without changing collision geometry.
MeshMaterialSections3D buildTerrainMaterialSections(
    const HeightField &field, std::size_t biome,
    std::span<const std::size_t> samples,
    const MeshMaterialSections3D *previous = nullptr);
MeshMaterialSections3D rebindTerrainMaterialSections(
    const HeightField &field, std::size_t biome,
    const MeshMaterialSections3D &previous);

} // namespace terrain_detail
} // namespace demi::runtime
