#pragma once

#include "demi/runtime/audio/AudioBackend.h"

#include <memory>

namespace demi::runtime {

[[nodiscard]] std::unique_ptr<AudioBackend> createMiniaudioAudioBackend();

} // namespace demi::runtime
