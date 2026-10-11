#pragma once

#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"

namespace demi::runtime::render {
class MaterialLibrary;

// Opaque sections with identical depth state cast one shadow for the whole mesh.
// Cutouts and differing raster states retain their individual draw ranges.
void compactOpaqueShadowSections3D(std::vector<VisibleMesh3D> &meshes,
                                    const MaterialLibrary &materials);
} // namespace demi::runtime::render
