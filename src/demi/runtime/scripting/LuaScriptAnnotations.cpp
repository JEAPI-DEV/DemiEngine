#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <fstream>
#include <optional>
#include <string_view>

namespace demi::runtime {

#if DEMI_HAS_LUA54
namespace {

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::optional<std::string> handleActionAnnotation(const std::string& line) {
  const std::string marker = "-- @HandleAction(";
  const std::size_t markerPos = line.find(marker);
  if (markerPos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t firstQuote = line.find('"', markerPos + marker.size());
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t secondQuote = line.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }
  return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::string> onEventAnnotation(const std::string& line) {
  const std::string marker = "-- @OnEvent(";
  const std::size_t markerPos = line.find(marker);
  if (markerPos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t firstQuote = line.find('"', markerPos + marker.size());
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t secondQuote = line.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }
  return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::string> annotatedFunctionName(const std::string& line) {
  constexpr std::string_view prefix = "function ";
  const std::string text = trim(line);
  if (!text.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::size_t nameStart = prefix.size();
  const std::size_t paren = text.find('(', nameStart);
  if (paren == std::string::npos) {
    return std::nullopt;
  }
  std::string name = trim(text.substr(nameStart, paren - nameStart));
  const std::size_t separator = name.find_last_of(".:");
  if (separator != std::string::npos) {
    name = name.substr(separator + 1);
  }
  if (name.empty()) {
    return std::nullopt;
  }
  return name;
}

} // namespace

std::vector<LuaActionHandler> parseActionHandlers(const std::filesystem::path& path) {
  std::vector<LuaActionHandler> handlers;
  std::ifstream input(path);
  if (!input) {
    return handlers;
  }

  std::vector<std::string> pendingActions;
  std::string line;
  while (std::getline(input, line)) {
    if (const std::optional<std::string> action = handleActionAnnotation(line)) {
      pendingActions.push_back(*action);
      continue;
    }

    if (pendingActions.empty()) {
      continue;
    }

    const std::string text = trim(line);
    if (text.empty() || text.starts_with("--")) {
      continue;
    }

    if (const std::optional<std::string> functionName = annotatedFunctionName(text)) {
      for (const std::string& action : pendingActions) {
        handlers.push_back({.action = action, .functionName = *functionName});
      }
    }
    pendingActions.clear();
  }
  return handlers;
}

std::vector<LuaEventHandler> parseEventHandlers(const std::filesystem::path& path) {
  std::vector<LuaEventHandler> handlers;
  std::ifstream input(path);
  if (!input) {
    return handlers;
  }

  std::vector<std::string> pendingEvents;
  std::string line;
  while (std::getline(input, line)) {
    if (const std::optional<std::string> eventName = onEventAnnotation(line)) {
      pendingEvents.push_back(*eventName);
      continue;
    }

    if (pendingEvents.empty()) {
      continue;
    }

    const std::string text = trim(line);
    if (text.empty() || text.starts_with("--")) {
      continue;
    }

    if (const std::optional<std::string> functionName = annotatedFunctionName(text)) {
      for (const std::string& eventName : pendingEvents) {
        handlers.push_back({.eventName = eventName, .functionName = *functionName});
      }
    }
    pendingEvents.clear();
  }
  return handlers;
}

std::string moduleNameFromProjectEntry(std::string module) {
  constexpr std::string_view scriptPrefix = "script://";
  if (module.starts_with(scriptPrefix)) {
    module = module.substr(scriptPrefix.size());
  }
  constexpr std::string_view scriptsPrefix = "scripts/";
  if (module.starts_with(scriptsPrefix)) {
    module = module.substr(scriptsPrefix.size());
  }
  constexpr std::string_view luaSuffix = ".lua";
  if (module.ends_with(luaSuffix)) {
    module.resize(module.size() - luaSuffix.size());
  }
  for (char& c : module) {
    if (c == '/' || c == '\\') {
      c = '.';
    }
  }
  return module;
}

std::string projectEntryToScriptUri(const std::string& module) {
  constexpr std::string_view scriptPrefix = "script://";
  if (module.starts_with(scriptPrefix)) {
    return module;
  }
  std::string path = module;
  for (char& c : path) {
    if (c == '.') {
      c = '/';
    }
  }
  return "script://scripts/" + path + ".lua";
}
#endif

} // namespace demi::runtime
