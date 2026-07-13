#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime::input {

struct InputReplay {
  float fixedTimestep = 1.0F / 60.0F;
  std::vector<InputState> frames;

  [[nodiscard]] bool apply(std::size_t frame, InputState &state) const;
};

[[nodiscard]] std::optional<InputReplay>
loadInputReplay(const std::filesystem::path &path, std::string &error);

} // namespace demi::runtime::input
