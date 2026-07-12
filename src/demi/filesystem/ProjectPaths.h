#pragma once

#include <filesystem>
#include <vector>

namespace demi {

[[nodiscard]] bool hasExtension(const std::filesystem::path &path,
                                const char *extension);
[[nodiscard]] bool isProjectFile(const std::filesystem::path &path);
[[nodiscard]] bool isSceneFile(const std::filesystem::path &path);
[[nodiscard]] bool isHudFile(const std::filesystem::path &path);
[[nodiscard]] bool isSaveFile(const std::filesystem::path &path);
[[nodiscard]] bool isAssetFile(const std::filesystem::path &path);
[[nodiscard]] bool isPrefabFile(const std::filesystem::path &path);
[[nodiscard]] std::vector<std::filesystem::path>
collectKnownSourceFiles(const std::filesystem::path &root);

} // namespace demi
