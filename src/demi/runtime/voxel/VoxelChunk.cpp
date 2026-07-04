#include "demi/runtime/voxel/VoxelChunk.h"

#include <algorithm>

namespace demi::runtime {

VoxelChunk::VoxelChunk(const int width, const int height, const int depth)
    : width_(std::max(width, 0)),
      height_(std::max(height, 0)),
      depth_(std::max(depth, 0)),
      blocks_(static_cast<std::size_t>(width_ * height_ * depth_), 0) {
}

int VoxelChunk::width() const {
  return width_;
}

int VoxelChunk::height() const {
  return height_;
}

int VoxelChunk::depth() const {
  return depth_;
}

bool VoxelChunk::contains(const int x, const int y, const int z) const {
  return x >= 0 && y >= 0 && z >= 0 && x < width_ && y < height_ && z < depth_;
}

std::uint16_t VoxelChunk::block(const int x, const int y, const int z) const {
  return contains(x, y, z) ? blocks_[static_cast<std::size_t>(index(x, y, z))] : 0;
}

void VoxelChunk::setBlock(const int x, const int y, const int z, const std::uint16_t blockId) {
  if (contains(x, y, z)) {
    blocks_[static_cast<std::size_t>(index(x, y, z))] = blockId;
  }
}

void VoxelChunk::fill(const std::uint16_t blockId) {
  std::ranges::fill(blocks_, blockId);
}

int VoxelChunk::index(const int x, const int y, const int z) const {
  return x + width_ * (z + depth_ * y);
}

} // namespace demi::runtime
