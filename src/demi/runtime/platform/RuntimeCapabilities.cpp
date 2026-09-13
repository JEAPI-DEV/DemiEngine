#include "demi/runtime/platform/RuntimeCapabilities.h"

#ifndef DEMI_ENABLE_GRAPHICS_RUNTIME
#define DEMI_ENABLE_GRAPHICS_RUNTIME 0
#endif

#ifndef DEMI_HAS_ENET
#define DEMI_HAS_ENET 0
#endif

#ifndef DEMI_HAS_FFMPEG
#define DEMI_HAS_FFMPEG 0
#endif

#ifndef DEMI_HAS_RSVG
#define DEMI_HAS_RSVG 0
#endif

namespace demi::runtime {

capabilities::RuntimeFeatures hostRuntimeFeatures() {
  return {.graphicsRuntime = DEMI_ENABLE_GRAPHICS_RUNTIME != 0,
          .network = DEMI_HAS_ENET != 0,
          .media = DEMI_HAS_FFMPEG != 0,
          .svg = DEMI_HAS_RSVG != 0};
}

} // namespace demi::runtime
