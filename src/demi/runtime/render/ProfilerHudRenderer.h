#pragma once

#include <string_view>

namespace demi::runtime {

void drawProfilerHud(std::string_view summary, int viewportWidth,
                     int viewportHeight);

} // namespace demi::runtime
