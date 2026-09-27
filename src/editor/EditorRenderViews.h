#pragma once
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

namespace demi::editor {
inline constexpr std::uint16_t EditorSceneFirstView = 1;
inline constexpr std::uint16_t EditorGameFirstView =
    EditorSceneFirstView + runtime::render::CameraViewCount3D;
inline constexpr std::uint16_t EditorGameLastView =
    EditorGameFirstView + runtime::render::CameraViewCount3D - 1;
} // namespace demi::editor
