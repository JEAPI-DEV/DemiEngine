#include "demi/runtime/scripting/annotations/OnEventAnnotation.h"
#include "demi/runtime/scripting/annotations/LuaAnnotationScanner.h"
namespace demi::runtime {
std::vector<LuaEventHandler>
OnEventAnnotation::parse(const std::filesystem::path &path) {
  std::vector<LuaEventHandler> handlers;
  for (LuaAnnotatedFunction &annotation :
       LuaAnnotationScanner::scan(path, "-- @OnEvent("))
    handlers.push_back({.eventName = std::move(annotation.value),
                        .functionName = std::move(annotation.functionName)});
  return handlers;
}
} // namespace demi::runtime
