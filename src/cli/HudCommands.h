#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

// `demi hud inspect <hud.json> [--format text|json]`: reports the canvas
// size and the resolved node rectangles computed by the runtime layout
// engine so tooling can target interactive nodes without guessing screen
// coordinates.
[[nodiscard]] int runHudCommand(const std::vector<std::string> &args,
                                std::ostream &out, std::ostream &error);

} // namespace demi::cli
