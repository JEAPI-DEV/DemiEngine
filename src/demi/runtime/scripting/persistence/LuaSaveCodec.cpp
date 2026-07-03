#include "demi/runtime/scripting/persistence/LuaSaveCodec.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace demi::runtime {
namespace {

constexpr int CurrentSaveFormatVersion = 2;

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

class SaveStateParser {
public:
  virtual ~SaveStateParser() = default;
  [[nodiscard]] virtual bool canParse(const std::string& text) const = 0;
  [[nodiscard]] virtual std::unordered_map<std::string, LuaScriptHost::SaveValue> parse(const std::string& text) const = 0;
};

class JsonSaveStateParser final : public SaveStateParser {
public:
  [[nodiscard]] bool canParse(const std::string& text) const override {
    return parseSaveDocument(text).has_value();
  }

  [[nodiscard]] std::unordered_map<std::string, LuaScriptHost::SaveValue> parse(const std::string& text) const override {
    std::unordered_map<std::string, LuaScriptHost::SaveValue> values;
    const std::optional<nlohmann::json> document = parseSaveDocument(text);
    if (!document.has_value()) {
      return values;
    }
    for (const auto& [key, value] : (*document)["state"].items()) {
      if (value.is_number()) {
        values[key] = LuaScriptHost::SaveValue{.value = value.dump(), .number = true};
      } else if (value.is_string()) {
        values[key] = LuaScriptHost::SaveValue{.value = value.get<std::string>(), .number = false};
      }
    }
    return values;
  }
};

class LegacySaveStateParser final : public SaveStateParser {
public:
  [[nodiscard]] bool canParse(const std::string& text) const override {
    const std::size_t stateKey = text.find("\"state\"");
    return stateKey != std::string::npos && text.find('{', stateKey) != std::string::npos;
  }

  [[nodiscard]] std::unordered_map<std::string, LuaScriptHost::SaveValue> parse(const std::string& text) const override {
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
};

} // namespace

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

std::filesystem::path savePath(const std::filesystem::path& projectDirectory, const std::string& slot) {
  return projectDirectory / "saves" / (sanitizedSaveSlot(slot) + ".save.json");
}

bool atomicWriteText(const std::filesystem::path& path, const std::string& text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }

  const std::filesystem::path tempPath = path.string() + ".tmp";
  {
    std::ofstream output(tempPath);
    if (!output) {
      return false;
    }
    output << text;
  }

  std::filesystem::rename(tempPath, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(tempPath, path, error);
  }
  return !error;
}

std::optional<nlohmann::json> parseSaveDocument(const std::string& text) {
  try {
    nlohmann::json document = nlohmann::json::parse(text);
    if (!document.is_object() || !document.contains("format_version") || !document.contains("state") || !document["state"].is_object()) {
      return std::nullopt;
    }
    return document;
  } catch (...) {
    return std::nullopt;
  }
}

std::unordered_map<std::string, LuaScriptHost::SaveValue> parseSaveState(const std::string& text) {
  const JsonSaveStateParser jsonParser;
  const LegacySaveStateParser legacyParser;
  const SaveStateParser* parsers[] = {&jsonParser, &legacyParser};
  for (const SaveStateParser* parser : parsers) {
    if (parser->canParse(text)) {
      return parser->parse(text);
    }
  }
  return {};
}

std::string serializeSaveSlotDocument(const std::string& safeSlot, const std::unordered_map<std::string, LuaScriptHost::SaveValue>& values) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"format_version\": " << CurrentSaveFormatVersion << ",\n";
  output << "  \"slot\": \"" << escapeJsonString(safeSlot) << "\",\n";
  output << "  \"state\": {\n";

  std::vector<std::string> keys;
  keys.reserve(values.size());
  for (const auto& [key, _] : values) {
    keys.push_back(key);
  }
  std::ranges::sort(keys);

  for (std::size_t i = 0; i < keys.size(); ++i) {
    const LuaScriptHost::SaveValue& value = values.at(keys[i]);
    output << "    \"" << escapeJsonString(keys[i]) << "\": ";
    output << (value.number ? value.value : "\"" + escapeJsonString(value.value) + "\"");
    output << (i + 1 < keys.size() ? ",\n" : "\n");
  }

  output << "  }\n";
  output << "}\n";
  return output.str();
}

} // namespace demi::runtime
