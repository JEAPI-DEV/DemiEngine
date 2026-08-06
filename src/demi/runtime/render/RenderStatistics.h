#pragma once

#include <cstddef>

namespace demi::runtime {

struct RenderStatistics {
  std::size_t batches = 0;
  std::size_t triangles = 0;
  std::size_t particles = 0;
  std::size_t lights = 0;
  std::size_t shadowPasses = 0;
  std::size_t renderTargetBytes = 0;

  void reset() { *this = {}; }
};

} // namespace demi::runtime
