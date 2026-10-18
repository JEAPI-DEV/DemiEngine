#include "demi/runtime/render/backend/BgfxInstanceBufferPool.h"
#include "demi/runtime/render/backend/BgfxFrameSerial.h"
#include <algorithm>

namespace demi::runtime::render {
BgfxInstanceBufferPool::BgfxInstanceBufferPool(
    const BgfxRenderCommandsOptions &options)
    : validOptions_(options.valid()),
      blockBytes_(options.instanceBufferChunkBytes),
      maximumBytes_(options.instanceBufferBudgetBytes) {}
BgfxInstanceBufferPool::~BgfxInstanceBufferPool() {
  if (bgfxSubmissionContextActive() && context_ == bgfxContextSerial()) {
    for (const auto &block : blocks_) {
      bgfx::destroy(block.handle);
    }
  }
}
void BgfxInstanceBufferPool::beginNativeFrame() {
  const auto context = bgfxContextSerial();
  if (context_ != context) {
    // Shutdown already retired these native handles. Never destroy/reuse their
    // numeric indices in a newly initialized bgfx context.
    blocks_.clear();
    residentBytes_ = 0;
    context_ = context;
    serial_ = UINT64_MAX;
  }
  const auto current = bgfxSubmissionSerial();
  if (serial_ == current) {
    return;
  }
  serial_ = current;
  uploadedBytes_ = 0;
  chunks_ = 0;
  for (auto &block : blocks_) {
    block.used = 0;
  }
}
std::size_t
BgfxInstanceBufferPool::remainingRecords(std::uint16_t stride) const {
  if (!validOptions_ || !bgfxSubmissionContextActive() || stride == 0 ||
      stride % 16 != 0 || blockBytes_ == 0 || maximumBytes_ < blockBytes_) {
    return 0;
  }
  if (context_ != bgfxContextSerial()) {
    return (maximumBytes_ / blockBytes_) * (blockBytes_ / stride);
  }
  const bool newFrame = serial_ != bgfxSubmissionSerial();
  std::size_t records = 0;
  for (const auto &block : blocks_) {
    const auto used = newFrame ? 0 : block.used;
    if (block.stride == stride) {
      records += block.capacity - used;
    } else if (used == 0) {
      records += blockBytes_ / stride;
    }
  }
  records +=
      ((maximumBytes_ - residentBytes_) / blockBytes_) * (blockBytes_ / stride);
  return records;
}
std::uint32_t
BgfxInstanceBufferPool::chunkCapacity(std::uint16_t stride,
                                      std::uint32_t requested) const {
  if (!validOptions_ || !bgfxSubmissionContextActive() || stride == 0 ||
      stride % 16 != 0 || requested == 0 || blockBytes_ == 0 ||
      maximumBytes_ < blockBytes_) {
    return 0;
  }
  if (context_ != bgfxContextSerial()) {
    return static_cast<std::uint32_t>(
        std::min(std::size_t(requested), blockBytes_ / stride));
  }
  const bool newFrame = serial_ != bgfxSubmissionSerial();
  std::size_t contiguous =
      residentBytes_ <= maximumBytes_ - blockBytes_ ? blockBytes_ / stride : 0;
  for (const auto &block : blocks_) {
    const auto used = newFrame ? 0 : block.used;
    const auto available = block.stride == stride ? block.capacity - used
                           : used == 0            ? blockBytes_ / stride
                                                  : 0;
    contiguous = std::max(contiguous, std::size_t(available));
  }
  return static_cast<std::uint32_t>(
      std::min(std::size_t(requested), contiguous));
}

std::string BgfxInstanceBufferPool::budgetError(std::uint16_t stride,
                                                std::size_t count) const {
  if (stride > blockBytes_) {
    return "Instance record stride " + std::to_string(stride) +
           " exceeds the configured " + std::to_string(blockBytes_) +
           " byte chunk. Increase "
           "BgfxRenderCommandsOptions::instanceBufferChunkBytes.";
  }
  return "Instance draw needs " + std::to_string(count * stride) + " bytes; " +
         std::to_string(remainingRecords(stride) * stride) +
         " remain in the configured " + std::to_string(maximumBytes_) +
         " byte native instance pool. Increase "
         "BgfxRenderCommandsOptions::instanceBufferBudgetBytes at renderer "
         "creation "
         "or reduce simultaneous view demand. UI transient memory is "
         "untouched.";
}

bool BgfxInstanceBufferPool::upload(std::uint16_t stride,
                                    std::span<const std::byte> records,
                                    Allocation &allocation,
                                    std::string &error) {
  if (!validOptions_) {
    error = "Invalid native instance pool options: use aligned chunks that "
            "divide the nonzero reservation budget.";
    return false;
  }
  if (!bgfxSubmissionContextActive()) {
    error = "Cannot upload instance data without an active native graphics "
            "context.";
    return false;
  }
  if (stride == 0 || stride % 16 != 0 || records.empty() ||
      records.size() % stride != 0 || records.size() > blockBytes_) {
    error = "Dedicated instance chunks require complete aligned records within "
            "the configured " +
            std::to_string(blockBytes_) + " byte chunk size.";
    return false;
  }
  beginNativeFrame();
  const auto count = static_cast<std::uint32_t>(records.size() / stride);
  for (auto &block : blocks_) {
    if (block.stride != stride || block.capacity - block.used < count) {
      continue;
    }
    allocation = {block.handle, block.used, count};
    bgfx::update(
        block.handle, block.used,
        bgfx::copy(records.data(), static_cast<std::uint32_t>(records.size())));
    block.used += count;
    uploadedBytes_ += records.size();
    ++chunks_;
    return true;
  }
  const auto recycled =
      std::find_if(blocks_.begin(), blocks_.end(), [&](const auto &block) {
        return block.used == 0 && block.stride != stride;
      });
  const bool recycle =
      residentBytes_ > maximumBytes_ - blockBytes_ && recycled != blocks_.end();
  if (residentBytes_ > maximumBytes_ - blockBytes_ && !recycle) {
    error = budgetError(stride, count);
    return false;
  }
  bgfx::VertexLayout layout;
  layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float);
  // setInstanceDataBuffer consumes the record stride, not vertex semantics.
  // Padding supports the same aligned generic payloads as the transient API.
  for (std::size_t remaining = stride - 16; remaining > 0;) {
    const auto padding =
        static_cast<std::uint8_t>(std::min<std::size_t>(remaining, 255));
    layout.skip(padding);
    remaining -= padding;
  }
  layout.end();
  const auto capacity = static_cast<std::uint32_t>(blockBytes_ / stride);
  const auto handle = bgfx::createDynamicVertexBuffer(capacity, layout);
  if (!bgfx::isValid(handle)) {
    error = "Could not allocate a " + std::to_string(blockBytes_) +
            " byte native instance block (reserved " +
            std::to_string(residentBytes_) + ", configured budget " +
            std::to_string(maximumBytes_) +
            "). Lower BgfxRenderCommandsOptions::instanceBufferChunkBytes or "
            "check device memory.";
    return false;
  }
  if (recycle) {
    bgfx::destroy(recycled->handle);
    *recycled = Block{handle, stride, capacity, count};
  } else {
    blocks_.push_back({handle, stride, capacity, count});
    residentBytes_ += blockBytes_;
  }
  bgfx::update(
      handle, 0,
      bgfx::copy(records.data(), static_cast<std::uint32_t>(records.size())));
  allocation = {handle, 0, count};
  uploadedBytes_ += records.size();
  ++chunks_;
  return true;
}
} // namespace demi::runtime::render
