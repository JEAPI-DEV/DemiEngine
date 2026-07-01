#include "demi/runtime/LuaScriptHost.h"

#include "demi/runtime/LuaScriptHostInternal.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace demi::runtime {
namespace {

std::string sanitizedSaveSlot(std::string slot) {
  if (slot.empty()) {
    return "settings";
  }
  for (char& c : slot) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (!std::isalnum(value) && c != '_' && c != '-') {
      c = '_';
    }
  }
  return slot;
}

std::string escapeJsonString(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::optional<std::size_t> matchingClose(const std::string& text, const std::size_t open, const char openChar, const char closeChar) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (std::size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && inString) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (c == openChar) {
      ++depth;
    } else if (c == closeChar) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::nullopt;
}

std::string unescapeJsonString(const std::string& text) {
  std::string unescaped;
  unescaped.reserve(text.size());
  bool escaped = false;
  for (const char c : text) {
    if (escaped) {
      unescaped.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    unescaped.push_back(c);
  }
  return unescaped;
}

std::unordered_map<std::string, LuaScriptHost::SaveValue> parseSaveState(const std::string& text) {
  std::unordered_map<std::string, LuaScriptHost::SaveValue> values;
  const std::size_t stateKey = text.find("\"state\"");
  const std::size_t open = stateKey == std::string::npos ? std::string::npos : text.find('{', stateKey);
  if (open == std::string::npos) {
    return values;
  }
  const std::optional<std::size_t> close = matchingClose(text, open, '{', '}');
  if (!close.has_value()) {
    return values;
  }

  std::size_t cursor = open + 1;
  while (cursor < *close) {
    const std::size_t keyStart = text.find('"', cursor);
    if (keyStart == std::string::npos || keyStart >= *close) {
      break;
    }
    const std::size_t keyEnd = text.find('"', keyStart + 1);
    const std::size_t colon = keyEnd == std::string::npos ? std::string::npos : text.find(':', keyEnd + 1);
    if (keyEnd == std::string::npos || colon == std::string::npos || colon >= *close) {
      break;
    }

    std::size_t valueStart = text.find_first_not_of(" \t\r\n", colon + 1);
    if (valueStart == std::string::npos || valueStart >= *close) {
      break;
    }

    const std::string key = unescapeJsonString(text.substr(keyStart + 1, keyEnd - keyStart - 1));
    if (text[valueStart] == '"') {
      const std::size_t valueEnd = text.find('"', valueStart + 1);
      if (valueEnd == std::string::npos || valueEnd >= *close) {
        break;
      }
      values[key] = LuaScriptHost::SaveValue{.value = unescapeJsonString(text.substr(valueStart + 1, valueEnd - valueStart - 1)), .number = false};
      cursor = valueEnd + 1;
    } else {
      const std::size_t valueEnd = text.find_first_of(",}\r\n", valueStart);
      const std::size_t end = valueEnd == std::string::npos ? *close : std::min(valueEnd, *close);
      std::string value = text.substr(valueStart, end - valueStart);
      while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
      }
      values[key] = LuaScriptHost::SaveValue{.value = value, .number = true};
      cursor = end + 1;
    }
  }
  return values;
}

} // namespace

std::unordered_map<std::string, LuaScriptHost::SaveValue>& LuaScriptHost::loadSaveSlot(const std::string& slot) {
  const std::string safeSlot = sanitizedSaveSlot(slot);
  if (auto found = saves_.find(safeSlot); found != saves_.end()) {
    return found->second;
  }

  std::unordered_map<std::string, SaveValue> values;
  const std::filesystem::path path = projectDirectory_ / "saves" / (safeSlot + ".save.json");
  std::ifstream input(path);
  if (input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    values = parseSaveState(buffer.str());
  }

  auto [inserted, _] = saves_.emplace(safeSlot, std::move(values));
  return inserted->second;
}

bool LuaScriptHost::writeSaveSlot(const std::string& slot) {
  if (projectDirectory_.empty()) {
    return false;
  }

  const std::string safeSlot = sanitizedSaveSlot(slot);
  auto found = saves_.find(safeSlot);
  if (found == saves_.end()) {
    return false;
  }

  const std::filesystem::path savesDirectory = projectDirectory_ / "saves";
  std::error_code error;
  std::filesystem::create_directories(savesDirectory, error);
  if (error) {
    return false;
  }

  std::ofstream output(savesDirectory / (safeSlot + ".save.json"));
  if (!output) {
    return false;
  }

  output << "{\n";
  output << "  \"format_version\": 1,\n";
  output << "  \"slot\": \"" << escapeJsonString(safeSlot) << "\",\n";
  output << "  \"state\": {\n";

  std::vector<std::string> keys;
  keys.reserve(found->second.size());
  for (const auto& [key, _] : found->second) {
    keys.push_back(key);
  }
  std::ranges::sort(keys);

  for (std::size_t i = 0; i < keys.size(); ++i) {
    const SaveValue& value = found->second.at(keys[i]);
    output << "    \"" << escapeJsonString(keys[i]) << "\": ";
    output << (value.number ? value.value : "\"" + escapeJsonString(value.value) + "\"");
    output << (i + 1 < keys.size() ? ",\n" : "\n");
  }

  output << "  }\n";
  output << "}\n";
  return true;
}

std::optional<float> LuaScriptHost::saveNumber(const std::string& slot, const std::string& key) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || !found->second.number) {
    return std::nullopt;
  }
  try {
    return std::stof(found->second.value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> LuaScriptHost::saveString(const std::string& slot, const std::string& key) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || found->second.number) {
    return std::nullopt;
  }
  return found->second.value;
}

bool LuaScriptHost::setSaveNumber(const std::string& slot, const std::string& key, const float value) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  std::ostringstream stream;
  stream << std::setprecision(6) << value;
  values[key] = SaveValue{.value = stream.str(), .number = true};
  return writeSaveSlot(slot);
}

bool LuaScriptHost::setSaveString(const std::string& slot, const std::string& key, const std::string& value) {
  std::unordered_map<std::string, SaveValue>& values = loadSaveSlot(slot);
  values[key] = SaveValue{.value = value, .number = false};
  return writeSaveSlot(slot);
}

Diagnostics LuaScriptHost::checkScriptSyntax(const std::filesystem::path& path) {
  Diagnostics diagnostics;
  if (!std::filesystem::exists(path)) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_NOT_FOUND", .message = "Lua script does not exist.", .path = path.string(), .suggestion = "Pass an existing .lua script path."});
    return diagnostics;
  }

#if !DEMI_HAS_LUA54
  diagnostics.push_back(Diagnostic{.severity = Severity::Warning, .code = "SCRIPT_CHECK_LUA_UNAVAILABLE", .message = "Lua 5.4 was not found at configure time, so syntax could not be checked.", .path = path.string(), .suggestion = "Install lua5.4 development files and re-run cmake --preset linux-debug."});
#else
  auto* state = luaL_newstate();
  if (state == nullptr) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_CHECK_LUA_ALLOC_FAILED", .message = "Failed to allocate a Lua state for syntax checking.", .path = path.string(), .suggestion = "Retry after freeing memory."});
    return diagnostics;
  }

  if (luaL_loadfile(state, path.string().c_str()) != LUA_OK) {
    diagnostics.push_back(Diagnostic{.severity = Severity::Error, .code = "SCRIPT_SYNTAX_ERROR", .message = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "Lua syntax error.", .path = path.string(), .suggestion = "Fix the Lua parser error and run demi script check again."});
    lua_pop(state, 1);
  }
  lua_close(state);
#endif
  return diagnostics;
}

} // namespace demi::runtime
