#pragma once
#include "demi/runtime/render/backend/BgfxRenderCommandsOptions.h"
#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime::render {

// Dedicated instance uploads never consume the transient vertex arena shared
// with UI. Ranges remain disjoint until the native device submits the frame.
class BgfxInstanceBufferPool {
public:
  struct Allocation {
    bgfx::DynamicVertexBufferHandle buffer = BGFX_INVALID_HANDLE;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
  };
  ~BgfxInstanceBufferPool();
  explicit BgfxInstanceBufferPool(
      const BgfxRenderCommandsOptions &options = {});
  BgfxInstanceBufferPool(const BgfxInstanceBufferPool &) = delete;
  BgfxInstanceBufferPool &operator=(const BgfxInstanceBufferPool &) = delete;
  std::uint32_t chunkCapacity(std::uint16_t stride,
                              std::uint32_t requested) const;
  std::size_t remainingRecords(std::uint16_t stride) const;
  std::size_t residentBytes() const { return residentBytes_; }
  std::size_t frameUploadBytes() const { return uploadedBytes_; }
  std::size_t frameChunks() const { return chunks_; }
  std::string budgetError(std::uint16_t stride, std::size_t count) const;
  bool upload(std::uint16_t stride, std::span<const std::byte> records,
              Allocation &allocation, std::string &error);

private:
  bool validOptions_;
  std::size_t blockBytes_;
  std::size_t maximumBytes_;
  struct Block {
    bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
    std::uint16_t stride = 0;
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
  };
  void beginNativeFrame();
  std::uint64_t serial_ = UINT64_MAX;
  std::uint64_t context_ = UINT64_MAX;
  std::size_t residentBytes_ = 0;
  std::size_t uploadedBytes_ = 0;
  std::size_t chunks_ = 0;
  std::vector<Block> blocks_;
};
} // namespace demi::runtime::render
