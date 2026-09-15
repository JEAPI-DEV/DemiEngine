#pragma once

#include <bgfx/bgfx.h>
#include <memory>

namespace demi::runtime::render {
// Profile-only callback for Android's single-threaded bgfx submission path.
std::unique_ptr<bgfx::CallbackI> makeBgfxProfileCallback();
} // namespace demi::runtime::render
