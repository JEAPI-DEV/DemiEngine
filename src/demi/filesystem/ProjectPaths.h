#pragma once

#include <filesystem>
#include <vector>
#include <string_view>

namespace demi {

[[nodiscard]] bool hasExtension(const std::filesystem::path &path,
                                const char *extension);
[[nodiscard]] bool isProjectFile(const std::filesystem::path &path);
[[nodiscard]] bool isSceneFile(const std::filesystem::path &path);
[[nodiscard]] bool isHudFile(const std::filesystem::path &path);
[[nodiscard]] bool isSaveFile(const std::filesystem::path &path);
[[nodiscard]] bool isAssetFile(const std::filesystem::path &path);
[[nodiscard]] bool isColliderShapeFile(const std::filesystem::path &path);
[[nodiscard]] bool isPrefabFile(const std::filesystem::path &path);
[[nodiscard]] bool isUiPrefabFile(const std::filesystem::path &path);
[[nodiscard]] bool isInputReplayFile(const std::filesystem::path &path);
[[nodiscard]] bool isPackageManifestFile(const std::filesystem::path &path);
[[nodiscard]] bool isAssetGroupFile(const std::filesystem::path &path);
// Implicit source discovery excludes tool state and generated output. Explicit
// file paths and lock-declared package content are loaded separately.
[[nodiscard]] bool isInternalProjectDirectory(std::string_view name);
[[nodiscard]] std::vector<std::filesystem::path>
collectKnownSourceFiles(const std::filesystem::path &root);

} // namespace demi
