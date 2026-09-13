#pragma once

#include <bx/bx.h>

#define IM_ASSERT(_EXPR) BX_ASSERT(_EXPR, "")
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS
#define IMGUI_DISABLE_STB_RECT_PACK_IMPLEMENTATION
#define IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION
#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS

// The bgfx renderer bridge needs this small font selector. Its optional legacy
// widget bundle is deliberately excluded: official docking owns dock state,
// and the unused range-slider/gizmo extensions depend on private older APIs.
namespace ImGui {

struct Font {
  enum Enum { Regular, Mono, Count };
};

void PushFont(Font::Enum font, float baseSize = 0.0F);

// Compatibility hooks called by bgfx's renderer bridge. Official docking is
// initialized and shut down with the ImGui context itself.
inline void InitDockContext() {}
inline void ShutdownDockContext() {}

} // namespace ImGui

namespace ImGuizmo {

inline void Create() {}
inline void Destroy() {}
inline void BeginFrame() {}

} // namespace ImGuizmo
