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
  std::unordered_map<std::string, LuaScriptHost::SaveValue> values;
  const std::optional<nlohmann::json> document = parseSaveDocument(text);
  if (!document.has_value())
    return values;
  for (const auto &[key, value] : (*document)["state"].items()) {
    if (value.is_number()) {
      values[key] =
          LuaScriptHost::SaveValue{.value = value.dump(), .number = true};
    } else if (value.is_string()) {
      values[key] = LuaScriptHost::SaveValue{
          .value = value.get<std::string>(), .number = false};
    }
  }
  return values;
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
