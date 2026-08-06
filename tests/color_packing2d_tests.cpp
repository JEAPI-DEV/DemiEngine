#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <cassert>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  const Color channelOrder{.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 0.4F};
  assert(packVertexColorRgba8(channelOrder) == 0x664d331aU);
  assert(packClearColorRgba8(channelOrder) == 0x1a334d66U);

  const Color arenaBackground{.r = 0.06F, .g = 0.07F, .b = 0.10F, .a = 1.0F};
  assert(packVertexColorRgba8(arenaBackground) == 0xff1a120fU);
  assert(packClearColorRgba8(arenaBackground) == 0x0f121affU);

  const Color outsideRange{.r = -1.0F, .g = 2.0F, .b = 0.5F, .a = 1.0F};
  assert(packVertexColorRgba8(outsideRange) == 0xff80ff00U);
  assert(packClearColorRgba8(outsideRange) == 0x00ff80ffU);
  return 0;
}
