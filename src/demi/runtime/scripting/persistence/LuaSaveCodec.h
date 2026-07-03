#pragma once

#include "demi/runtime/scripting/LuaScriptHost.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

namespace demi::runtime {

[[nodiscard]] std::string sanitizedSaveSlot(std::string slot);
[[nodiscard]] std::filesystem::path savePath(const std::filesystem::path& projectDirectory, const std::string& slot);
[[nodiscard]] bool atomicWriteText(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::optional<nlohmann::json> parseSaveDocument(const std::string& text);
[[nodiscard]] std::unordered_map<std::string, LuaScriptHost::SaveValue> parseSaveState(const std::string& text);
[[nodiscard]] std::string serializeSaveSlotDocument(const std::string& safeSlot, const std::unordered_map<std::string, LuaScriptHost::SaveValue>& values);

} // namespace demi::runtime
