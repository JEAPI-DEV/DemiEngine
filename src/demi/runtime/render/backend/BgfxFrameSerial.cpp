#include "demi/runtime/render/backend/BgfxFrameSerial.h"
namespace demi::runtime::render {
namespace {
std::uint64_t submissionSerial = 0;
std::uint64_t contextSerial = 0;
bool contextActive = false;
} // namespace
std::uint64_t bgfxSubmissionSerial() { return submissionSerial; }
void advanceBgfxSubmissionSerial() { ++submissionSerial; }
std::uint64_t bgfxContextSerial() { return contextSerial; }
bool bgfxSubmissionContextActive() { return contextActive; }
void beginBgfxSubmissionContext() {
  ++contextSerial;
  contextActive = true;
}
void endBgfxSubmissionContext() { contextActive = false; }
} // namespace demi::runtime::render
