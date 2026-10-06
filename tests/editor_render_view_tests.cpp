#include "editor/EditorRenderViews.h"

#include <cstddef>
#include <cstdint>

namespace {

using namespace demi::editor;
using demi::runtime::render::CameraViewCount3D;

// These invariants require no native window, graphics device or ImGui context.
constexpr bool authoredRangesAreIndependent() {
  for (std::size_t index = 0; index < EditorAuthoringViews.size(); ++index) {
    const auto view = EditorAuthoringViews[index];
    if (static_cast<std::size_t>(view) != index)
      return false;
    const std::uint16_t first = editorAuthoringFirstView(view);
    const std::uint16_t last = first + CameraViewCount3D - 1;
    if (first == 0 || last >= EditorGameFirstView)
      return false;
    for (std::size_t other = index + 1; other < EditorAuthoringViews.size();
         ++other) {
      if (last >= editorAuthoringFirstView(EditorAuthoringViews[other]))
        return false;
    }
  }
  return true;
}

static_assert(EditorAuthoringViews.size() == 4);
static_assert(editorAuthoringFirstView(EditorAuthoringView::Viewport) == 1);
static_assert(editorAuthoringFirstView(EditorAuthoringView::Prefab) ==
              1 + CameraViewCount3D);
static_assert(editorAuthoringFirstView(EditorAuthoringView::Hud) ==
              1 + 2 * CameraViewCount3D);
static_assert(editorAuthoringFirstView(EditorAuthoringView::TerrainAsset) ==
              1 + 3 * CameraViewCount3D);
static_assert(authoredRangesAreIndependent());
static_assert(EditorGameFirstView ==
              editorAuthoringFirstView(EditorAuthoringView::TerrainAsset) +
                  CameraViewCount3D);
static_assert(EditorGameLastView ==
              EditorGameFirstView + CameraViewCount3D - 1);
static_assert(EditorGameLastView < 255);

} // namespace

int main() { return 0; }
