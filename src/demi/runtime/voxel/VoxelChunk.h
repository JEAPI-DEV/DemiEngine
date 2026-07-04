#pragma once

#include <cstdint>
#include <vector>

namespace demi::runtime {

class VoxelChunk {
public:
  VoxelChunk() = default;
  VoxelChunk(int width, int height, int depth);

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] int depth() const;
  [[nodiscard]] bool contains(int x, int y, int z) const;
  [[nodiscard]] std::uint16_t block(int x, int y, int z) const;
  void setBlock(int x, int y, int z, std::uint16_t blockId);
  void fill(std::uint16_t blockId);

private:
  [[nodiscard]] int index(int x, int y, int z) const;

  int width_ = 0;
  int height_ = 0;
  int depth_ = 0;
  std::vector<std::uint16_t> blocks_;
};

} // namespace demi::runtime
