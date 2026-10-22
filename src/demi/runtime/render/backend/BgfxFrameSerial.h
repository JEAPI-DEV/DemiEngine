#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace demi::runtime::render {
// bgfx owns one process-wide submission context. Only the native device
// advances this serial, after an actual frame submission (never a camera pass).
std::uint64_t bgfxSubmissionSerial();
void advanceBgfxSubmissionSerial();
std::uint64_t bgfxContextSerial();
bool bgfxSubmissionContextActive();
void beginBgfxSubmissionContext();
void endBgfxSubmissionContext();
// Palette colors are shared by every renderer in one native frame. Index255
// means no attachment clear and is never allocated. Main-thread submission
// only.
std::optional<std::uint8_t> reserveBgfxClearPalette(std::size_t count);
} // namespace demi::runtime::render
