#include "demi/runtime/render/ProfilerHudRenderer.h"

#include "demi/runtime/profiling/ProfilerHudLayout.h"

#include <raylib.h>

#include <string>

namespace demi::runtime {

void drawProfilerHud(const std::string_view summary, const int viewportWidth,
                     const int viewportHeight) {
  const ProfilerHudLayout layout =
      profilerHudLayout(viewportWidth, viewportHeight);
  std::string visibleSummary(summary);
  const int availableTextWidth = layout.width - layout.padding * 2;
  while (!visibleSummary.empty() &&
         MeasureText(visibleSummary.c_str(), layout.fontSize) >
             availableTextWidth) {
    const std::size_t separator = visibleSummary.rfind(", ");
    if (separator == std::string::npos) {
      visibleSummary.resize(visibleSummary.size() - 1);
    } else {
      visibleSummary.resize(separator);
    }
  }
  DrawRectangle(layout.margin, layout.margin, layout.width, layout.height,
                {10, 12, 20, 225});
  DrawText(visibleSummary.c_str(), layout.margin + layout.padding,
           layout.margin + layout.padding, layout.fontSize,
           {120, 240, 210, 255});
}

} // namespace demi::runtime
