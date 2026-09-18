#include "demi/runtime/render/backend/BgfxProfileCallback.h"
#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace demi::runtime::render {
namespace {
const char *scopeName(std::string_view name) {
  if (name == "vkSuboptimalUsable")
    return "Vulkan.suboptimal_usable";
  if (name == "SwapChainVK::createSwapchain")
    return "Vulkan.swapchain_create";
  if (name == "SwapChainVK::update")
    return "Vulkan.swapchain_update";
  if (name == "FrameBufferVK::preReset")
    return "Vulkan.framebuffer_pre_reset";
  if (name == "vkUniformFlush")
    return "Vulkan.uniform_flush";
  if (name == "TimerQueryVK::end")
    return "Vulkan.timer_end";
  if (name == "TimerQueryVK::begin")
    return "Vulkan.timer_begin";
  if (name == "stagingScratchBuffer::flush")
    return "Vulkan.staging_flush";
  if (name == "CommandQueueVK::kick")
    return "Vulkan.command_kick";
  if (name == "vkMemoryBudget")
    return "Vulkan.memory_budget";
  if (name == "bgfx/API thread frame")
    return "Bgfx.api_frame";
  if (name == "bgfx::renderFrame")
    return "Bgfx.render_frame";
  if (name == "bgfx/flip")
    return "Bgfx.flip";
  if (name == "bgfx/Exec commands pre")
    return "Bgfx.commands_pre";
  if (name == "bgfx/Exec commands post")
    return "Bgfx.commands_post";
  if (name == "bgfx/Render submit")
    return "Bgfx.render_submit";
  if (name == "bgfx/Sort")
    return "Bgfx.sort";
  if (name == "bgfx/DedupBind")
    return "Bgfx.dedup_bind";
  if (name == "vkAcquireNextImageKHR")
    return "Vulkan.acquire_image";
  // Spelling in the pinned bgfx implementation.
  if (name == "vkQueuePresentHKR")
    return "Vulkan.present";
  if (name == "vkWaitForFences")
    return "Vulkan.wait_fence";
  if (name == "vkQueueSubmit")
    return "Vulkan.submit";
  if (name == "CommandQueueVK::alloc")
    return "Vulkan.command_alloc";
  if (name == "SwapChainVK::acquire")
    return "Vulkan.acquire_total";
  return nullptr;
}

class ProfileCallback final : public bgfx::CallbackI {
public:
  void fatal(const char *, uint16_t, bgfx::Fatal::Enum,
             const char *message) override {
    deviceLogError(message);
    std::abort();
  }
  void traceVargs(const char *, uint16_t, const char *format,
                  va_list args) override {
    std::array<char, 2048> text{};
    std::vsnprintf(text.data(), text.size(), format, args);
    deviceLog(text.data());
  }
  void profilerBegin(const char *name, uint32_t, const char *,
                     uint16_t) override {
    // RuntimeProfiler is main-thread-owned. Ignore background callbacks without
    // touching the stack; never merge other threads into this frame's timings.
    if (std::this_thread::get_id() != owner_)
      return;
    if (depth_ < stack_.size()) {
      auto &entry = stack_[depth_];
      entry.name = scopeName(name);
      if (entry.name)
        entry.start = std::chrono::steady_clock::now();
    }
    ++depth_;
  }
  void profilerBeginLiteral(const char *name, uint32_t color, const char *file,
                            uint16_t line) override {
    profilerBegin(name, color, file, line);
  }
  void profilerEnd() override {
    if (std::this_thread::get_id() != owner_ || depth_ == 0)
      return;
    --depth_;
    if (depth_ >= stack_.size())
      return;
    const auto &entry = stack_[depth_];
    if (entry.name)
      RuntimeProfiler::record(
          entry.name, std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - entry.start)
                          .count());
  }
  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
  void cacheWrite(uint64_t, const void *, uint32_t) override {}
  void screenShot(const char *, uint32_t, uint32_t, uint32_t,
                  bgfx::TextureFormat::Enum, const void *, uint32_t,
                  bool) override {
    deviceLogError(
        "Profile callback does not write screenshots; use device capture.");
  }
  void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum,
                    bool) override {
    deviceLogError("Profile callback does not support video capture.");
  }
  void captureEnd() override {}
  void captureFrame(const void *, uint32_t) override {}

private:
  struct Entry {
    const char *name = nullptr;
    std::chrono::steady_clock::time_point start;
  };
  const std::thread::id owner_ = std::this_thread::get_id();
  std::array<Entry, 64> stack_{};
  std::size_t depth_ = 0;
};
} // namespace
std::unique_ptr<bgfx::CallbackI> makeBgfxProfileCallback() {
  return std::make_unique<ProfileCallback>();
}
} // namespace demi::runtime::render
