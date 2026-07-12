#pragma once
#include "demi/runtime/scene/model/ProjectData.h"
#include <filesystem>
#include <vector>
namespace demi::runtime {
class OnEventAnnotation {
public:
  [[nodiscard]] static std::vector<LuaEventHandler>
  parse(const std::filesystem::path &path);
};
} // namespace demi::runtime
