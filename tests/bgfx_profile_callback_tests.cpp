#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/backend/BgfxProfileCallback.h"

#include <iostream>
#include <thread>

int main() {
  using demi::runtime::RuntimeProfiler;
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::resetSession();
  RuntimeProfiler::beginFrame();
  auto callback = demi::runtime::render::makeBgfxProfileCallback();
  callback->profilerBeginLiteral("SwapChainVK::acquire", 0, "", 0);
  callback->profilerBegin("ignored", 0, "", 0);
  callback->profilerBeginLiteral("vkAcquireNextImageKHR", 0, "", 0);
  callback->profilerEnd();
  callback->profilerEnd();
  callback->profilerEnd();
  std::thread other([&] {
    callback->profilerBeginLiteral("vkWaitForFences", 0, "", 0);
    callback->profilerEnd();
  });
  other.join();
  callback->profilerBeginLiteral("vkQueuePresentHKR", 0, "", 0);
  for (int i = 0; i < 80; ++i)
    callback->profilerBeginLiteral("ignored", 0, "", 0);
  for (int i = 0; i < 81; ++i)
    callback->profilerEnd();
  callback->profilerEnd(); // Unmatched end must not underflow.
  const auto entries = RuntimeProfiler::frameEntries();
  if (entries.size() != 3) {
    std::cerr << "Unexpected callback scope population\n";
    return 1;
  }
  for (const auto &entry : entries) {
    if (entry.calls != 1 || entry.totalMilliseconds < 0 ||
        (entry.name != "Vulkan.acquire_total" &&
         entry.name != "Vulkan.acquire_image" &&
         entry.name != "Vulkan.present")) {
      std::cerr << "Invalid callback timing: " << entry.name << '\n';
      return 1;
    }
  }
  std::cout << "bgfx callback scope/thread/overflow checks passed\n";
}
