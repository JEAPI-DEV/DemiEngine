#pragma once
#include <cstdint>

namespace demi::runtime::render {
// bgfx owns one process-wide submission context. Only the native device
// advances this serial, after an actual frame submission (never a camera pass).
std::uint64_t bgfxSubmissionSerial();
void advanceBgfxSubmissionSerial();
std::uint64_t bgfxContextSerial();
bool bgfxSubmissionContextActive();
void beginBgfxSubmissionContext();
void endBgfxSubmissionContext();
} // namespace demi::runtime::render
