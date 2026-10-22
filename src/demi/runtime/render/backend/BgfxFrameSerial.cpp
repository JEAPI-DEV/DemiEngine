#include "demi/runtime/render/backend/BgfxFrameSerial.h"
namespace demi::runtime::render {
namespace {
std::uint64_t submissionSerial = 0;
std::uint64_t contextSerial = 0;
bool contextActive = false;
std::size_t clearPaletteUsed = 0;
} // namespace
std::uint64_t bgfxSubmissionSerial() { return submissionSerial; }
void advanceBgfxSubmissionSerial() {
  ++submissionSerial;
  clearPaletteUsed = 0;
}
std::uint64_t bgfxContextSerial() { return contextSerial; }
bool bgfxSubmissionContextActive() { return contextActive; }
void beginBgfxSubmissionContext() {
  ++contextSerial;
  contextActive = true;
  clearPaletteUsed = 0;
}
void endBgfxSubmissionContext() {
  contextActive = false;
  clearPaletteUsed = 0;
}
std::optional<std::uint8_t> reserveBgfxClearPalette(std::size_t count) {
  if (!contextActive || count > UINT8_MAX - clearPaletteUsed) {
    return std::nullopt;
  }
  const auto first = static_cast<std::uint8_t>(clearPaletteUsed);
  clearPaletteUsed += count;
  return first;
}
} // namespace demi::runtime::render
