#pragma once
#include "demi/runtime/platform/PlatformHost.h"

namespace demi::runtime {
void recordPlatformFrameTiming(const platform::PlatformFrameState &state,
                               bool firstFrame);
}
