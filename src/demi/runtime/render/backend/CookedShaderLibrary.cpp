#include "demi/runtime/render/backend/CookedShaderLibrary.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace demi::runtime::render {
namespace {

std::vector<std::byte> readBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return {};
  const std::vector<char> chars((std::istreambuf_iterator<char>(input)), {});
  std::vector<std::byte> bytes(chars.size());
  std::transform(chars.begin(), chars.end(), bytes.begin(), [](char value) {
    return static_cast<std::byte>(static_cast<unsigned char>(value));
  });
  return bytes;
}

bool validRelativePath(const std::filesystem::path &,
                       const std::filesystem::path &relative) {
  if (relative.empty() || relative.is_absolute())
    return false;
  const auto normalized = relative.lexically_normal();
  return normalized != ".." &&
         (normalized.empty() || *normalized.begin() != "..");
}

} // namespace

CookedShaderLibrary::CookedShaderLibrary(GpuResources &resources)
    : resources_(resources) {}

CookedShaderLibrary::~CookedShaderLibrary() { clear(); }

bool CookedShaderLibrary::load(const AssetRegistry &registry,
                               std::vector<std::string> &diagnostics) {
  const auto manifestPath = registry.projectDirectory / "cook.manifest.json";
  if (!std::filesystem::is_regular_file(manifestPath)) {
    // Source projects without authored shaders legitimately have no cook
    // manifest. RuntimeApp cooks shader-bearing projects before this point.
    clear();
    return true;
  }

  nlohmann::json manifest;
  try {
    std::ifstream input(manifestPath);
    if (!input) {
      diagnostics.push_back("Could not open cooked shader manifest: " +
                            manifestPath.string());
      return false;
    }
    input >> manifest;
  } catch (const std::exception &exception) {
    diagnostics.push_back("Invalid cooked shader manifest: " +
                          std::string(exception.what()));
    return false;
  }
  if (manifest.value("format_version", 0) != 1 ||
      !manifest.contains("shader_programs") ||
      !manifest["shader_programs"].is_array()) {
    diagnostics.push_back("Cooked shader manifest has an unsupported format.");
    return false;
  }

  std::string backend(resources_.shaderBackend());
  // Noop validates ownership and command behavior but has no shaderc target.
  // Prefer a desktop binary so headless tests can still exercise loading.
  if (backend == "noop")
    backend = manifest.value("platform", "linux") == "android" ? "opengles"
                                                                  : "opengl";

  std::unordered_map<std::string, ProgramHandle> replacements;
  std::unordered_set<std::string> seen;
  for (const auto &entry : manifest["shader_programs"]) {
    if (!entry.is_object() || entry.value("backend", "") != backend)
      continue;
    const std::string asset = entry.value("asset", "");
    const std::filesystem::path vertex = entry.value("vertex", "");
    const std::filesystem::path fragment = entry.value("fragment", "");
    if (asset.empty() || !seen.insert(asset).second ||
        !validRelativePath(registry.projectDirectory, vertex) ||
        !validRelativePath(registry.projectDirectory, fragment)) {
      diagnostics.push_back("Invalid or duplicate cooked shader entry for " +
                            (asset.empty() ? std::string("<empty>") : asset) +
                            ".");
      for (const auto &[unused, handle] : replacements)
        resources_.destroy(handle);
      return false;
    }
    const auto vertexBytes = readBytes(registry.projectDirectory / vertex);
    const auto fragmentBytes = readBytes(registry.projectDirectory / fragment);
    if (vertexBytes.empty() || fragmentBytes.empty()) {
      diagnostics.push_back(asset + ": cooked shader binary is missing.");
      for (const auto &[unused, handle] : replacements)
        resources_.destroy(handle);
      return false;
    }
    std::string error;
    const ProgramHandle program = resources_.createProgram(
        {.vertexShader = vertexBytes,
         .fragmentShader = fragmentBytes,
         .debugName = asset},
        error);
    if (!program) {
      diagnostics.push_back(asset + ": " + error);
      for (const auto &[unused, handle] : replacements)
        resources_.destroy(handle);
      return false;
    }
    replacements.emplace(asset, program);
  }

  for (const AssetManifest &asset : registry.assets)
    if (asset.type == "Shader" && !replacements.contains(asset.id)) {
      diagnostics.push_back(asset.id +
                            ": no cooked shader program matches backend " +
                            backend + ".");
      for (const auto &[unused, handle] : replacements)
        resources_.destroy(handle);
      return false;
    }

  clear();
  programs_ = std::move(replacements);
  return true;
}

void CookedShaderLibrary::clear() {
  for (const auto &[unused, program] : programs_)
    resources_.destroy(program);
  programs_.clear();
}

ProgramHandle CookedShaderLibrary::find(const std::string_view assetId) const {
  const auto found = programs_.find(std::string(assetId));
  return found == programs_.end() ? ProgramHandle{} : found->second;
}

} // namespace demi::runtime::render
