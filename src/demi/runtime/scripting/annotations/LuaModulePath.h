#pragma once
#include <string>
namespace demi::runtime {
class LuaModulePath {
public:
  [[nodiscard]] static std::string moduleName(std::string projectEntry);
  [[nodiscard]] static std::string scriptUri(const std::string &projectEntry);
};
} // namespace demi::runtime
