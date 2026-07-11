#include "demi/runtime/scripting/annotations/LuaModulePath.h"
namespace demi::runtime {
std::string LuaModulePath::moduleName(std::string entry) {
  if (entry.starts_with("script://"))
    entry = entry.substr(9);
  if (entry.starts_with("scripts/"))
    entry = entry.substr(8);
  if (entry.ends_with(".lua"))
    entry.resize(entry.size() - 4);
  for (char &character : entry)
    if (character == '/' || character == '\\')
      character = '.';
  return entry;
}
std::string LuaModulePath::scriptUri(const std::string &entry) {
  if (entry.starts_with("script://"))
    return entry;
  std::string path = entry;
  for (char &character : path)
    if (character == '.')
      character = '/';
  return "script://scripts/" + path + ".lua";
}
} // namespace demi::runtime
