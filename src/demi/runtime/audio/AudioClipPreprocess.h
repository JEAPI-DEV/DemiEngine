#pragma once

#include <cstdint>

#if DEMI_HAS_MINIAUDIO
typedef struct ma_sound ma_sound;
#endif

namespace demi::runtime {

#if DEMI_HAS_MINIAUDIO
[[nodiscard]] std::uint64_t detectLeadingSilenceFrames(ma_sound* sound);
#endif

} // namespace demi::runtime
