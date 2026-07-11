#pragma once
#include "demi/runtime/scene/model/ProjectData.h"
#include <filesystem>
#include <vector>
namespace demi::runtime {
class HandleActionAnnotation {
public:
  [[nodiscard]] static std::vector<LuaActionHandler>
  parse(const std::filesystem::path &path);
};
} // namespace demi::runtime
