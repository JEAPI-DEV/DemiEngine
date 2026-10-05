#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace demi::editor {

enum class EditorAuthoringView { Viewport, Prefab, Hud, TerrainAsset };

inline constexpr std::array EditorAuthoringViews{
    EditorAuthoringView::Viewport, EditorAuthoringView::Prefab,
    EditorAuthoringView::Hud, EditorAuthoringView::TerrainAsset};

// View zero clears the host backbuffer. Each authored renderer needs its own
// complete camera range, including shadow cascades and resolve/post passes.
[[nodiscard]] constexpr std::uint16_t
editorAuthoringFirstView(const EditorAuthoringView view) {
  return static_cast<std::uint16_t>(1 + static_cast<std::size_t>(view) *
                                            runtime::render::CameraViewCount3D);
}

inline constexpr std::uint16_t EditorGameFirstView =
    1 + EditorAuthoringViews.size() * runtime::render::CameraViewCount3D;
inline constexpr std::uint16_t EditorGameLastView =
    EditorGameFirstView + runtime::render::CameraViewCount3D - 1;

static_assert(EditorGameLastView < 255, "Editor rendering must precede ImGui");

} // namespace demi::editor
