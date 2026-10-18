#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace demi::runtime::render {
// Native upload policy, independent of authored scenes. Reservation grows on
// demand to a high-water mark; budget must be a multiple of chunk size.
struct BgfxRenderCommandsOptions {
  std::size_t instanceBufferBudgetBytes = 64 * 1024 * 1024;
  std::size_t instanceBufferChunkBytes = 1024 * 1024;

  bool valid() const {
    return instanceBufferChunkBytes >= 16 &&
           instanceBufferChunkBytes % 16 == 0 &&
           instanceBufferChunkBytes <=
               std::numeric_limits<std::uint32_t>::max() &&
           instanceBufferBudgetBytes >= instanceBufferChunkBytes &&
           instanceBufferBudgetBytes % instanceBufferChunkBytes == 0;
  }
};
} // namespace demi::runtime::render
