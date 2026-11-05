#pragma once

#include <cstdint>

typedef struct ma_sound ma_sound;

namespace demi::runtime {

[[nodiscard]] std::uint64_t detectLeadingSilenceFrames(ma_sound* sound);

} // namespace demi::runtime
