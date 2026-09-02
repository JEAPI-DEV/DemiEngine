#pragma once

#include "demi/capabilities/PlatformCapabilities.h"

namespace demi::runtime {

// Reports the optional features compiled into the runtime library that owns
// this translation unit. The renderer runtime reports the host build's
// graphics, networking, media, and SVG support; the dedicated-server
// runtime reports the same options without a graphics runtime.
[[nodiscard]] capabilities::RuntimeFeatures hostRuntimeFeatures();

} // namespace demi::runtime
