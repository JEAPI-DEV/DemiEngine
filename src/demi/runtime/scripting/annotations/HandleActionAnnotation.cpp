#include "demi/runtime/scripting/annotations/HandleActionAnnotation.h"
#include "demi/runtime/scripting/annotations/LuaAnnotationScanner.h"
namespace demi::runtime {
std::vector<LuaActionHandler>
HandleActionAnnotation::parse(const std::filesystem::path &path) {
  std::vector<LuaActionHandler> handlers;
  for (LuaAnnotatedFunction &annotation :
       LuaAnnotationScanner::scan(path, "-- @HandleAction("))
    handlers.push_back({.action = std::move(annotation.value),
                        .functionName = std::move(annotation.functionName)});
  return handlers;
}
} // namespace demi::runtime
