#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

struct LuaAnnotatedFunction {
  std::string value;
  std::string functionName;
};

class LuaAnnotationScanner {
public:
  [[nodiscard]] static std::vector<LuaAnnotatedFunction>
  scan(const std::filesystem::path &path, std::string_view marker);
};

} // namespace demi::runtime
