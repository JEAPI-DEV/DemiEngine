#include "demi/assets/AssetCooker.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/RenderAsset.h"
#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace demi::assets {
namespace {

#ifndef DEMI_SHADERC_PATH
#define DEMI_SHADERC_PATH ""
#endif
#ifndef DEMI_BGFX_SHADER_INCLUDE_DIR
#define DEMI_BGFX_SHADER_INCLUDE_DIR ""
#endif

bool skippedRoot(const std::filesystem::path &relative) {
  if (relative.empty())
    return false;
  const std::string first = relative.begin()->string();
  return first == "assets" || first == "build" || first == "generated" ||
         first == ".git" || first == "saves" || first == "tests";
}

void addProjectFiles(const std::filesystem::path &projectDirectory,
                     std::set<std::filesystem::path> &files,
                     Diagnostics &diagnostics) {
  std::error_code code;
  for (std::filesystem::recursive_directory_iterator
           iterator(projectDirectory, code),
       end;
       iterator != end; iterator.increment(code)) {
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_SCAN_FAILED",
                             .message = code.message(),
                             .path = projectDirectory.string()});
      return;
    }
    const auto relative =
        std::filesystem::relative(iterator->path(), projectDirectory, code);
    if (skippedRoot(relative)) {
      if (iterator->is_directory())
        iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file())
      files.insert(iterator->path());
  }
}

std::string shellQuote(const std::filesystem::path &path) {
  std::string result = "'";
  for (const char character : path.string())
    result += character == '\'' ? "'\\''" : std::string(1, character);
  return result + "'";
}

struct ShaderVariant {
  const char *backend;
  const char *shadercPlatform;
  const char *profile;
};

std::vector<ShaderVariant> shaderVariants(const std::string &platform) {
  if (platform == "linux")
    return {{"vulkan", "linux", "spirv"}, {"opengl", "linux", "140"}};
  if (platform == "android")
    return {{"vulkan", "android", "spirv"},
            {"opengles", "android", "300_es"}};
  return {};
}

std::string safeAssetName(std::string id) {
  for (char &character : id)
    if (!std::isalnum(static_cast<unsigned char>(character)) &&
        character != '-' && character != '_')
      character = '_';
  return id;
}

bool usesUnifiedShaderSource(const ShaderAsset &shader) {
  return shader.stages.vertex.extension() == ".sc" &&
         shader.stages.fragment.extension() == ".sc";
}

bool compileShaderStage(const CookRequest &request,
                        const std::filesystem::path &source,
                        const std::filesystem::path &varying,
                        const std::filesystem::path &output, const char *type,
                        const ShaderVariant &variant,
                        Diagnostics &diagnostics) {
  std::filesystem::create_directories(output.parent_path());
  std::ostringstream command;
  command << shellQuote(request.shaderCompiler) << " -f "
          << shellQuote(source) << " -o " << shellQuote(output) << " --type "
          << type << " --platform " << variant.shadercPlatform << " -p "
          << variant.profile << " --varyingdef " << shellQuote(varying)
          << " -i " << shellQuote(source.parent_path());
  if (!request.shaderIncludeDirectory.empty())
    command << " -i " << shellQuote(request.shaderIncludeDirectory);
  command << " --Werror";
  if (std::system(command.str().c_str()) == 0 &&
      std::filesystem::is_regular_file(output))
    return true;
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = "COOK_SHADER_COMPILE_FAILED",
       .message = "bgfx shaderc failed for the " + std::string(type) +
                  " stage targeting " + variant.backend + ".",
       .path = source.string(),
       .suggestion = "Run demi cook again and fix the shaderc diagnostics."});
  return false;
}

} // namespace

Diagnostics cookProject(const CookRequest &request) {
  Diagnostics diagnostics;
  if (request.platform != "linux" && request.platform != "linux_server" &&
      request.platform != "android") {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "COOK_PLATFORM_UNSUPPORTED",
         .message =
             "Cooking is not implemented for platform: " + request.platform,
         .path = request.projectFile.string(),
         .suggestion = "Use --platform linux, linux_server, or android."});
    return diagnostics;
  }
  const auto absoluteProject = std::filesystem::absolute(request.projectFile);
  CookRequest effectiveRequest = request;
  if (effectiveRequest.shaderCompiler.empty())
    effectiveRequest.shaderCompiler = DEMI_SHADERC_PATH;
  if (effectiveRequest.shaderIncludeDirectory.empty())
    effectiveRequest.shaderIncludeDirectory = DEMI_BGFX_SHADER_INCLUDE_DIR;
  const auto summary = validatePath(absoluteProject);
  diagnostics.insert(diagnostics.end(), summary.diagnostics.begin(),
                     summary.diagnostics.end());
  const auto projectDirectory = absoluteProject.parent_path();
  const AssetRegistry registry = loadAssetRegistry(projectDirectory);
  if (hasErrors(diagnostics))
    return diagnostics;

  std::set<std::filesystem::path> files;
  addProjectFiles(projectDirectory, files, diagnostics);
  for (const auto &asset : registry.assets)
    for (const auto &file : collectAssetFiles(asset)) {
      if (!pathIsInside(projectDirectory, file))
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "COOK_EXTERNAL_ASSET_FILE",
                               .message = "Cooked assets must be inside the "
                                          "project directory.",
                               .path = file.string()});
      else
        files.insert(file);
    }
  if (hasErrors(diagnostics))
    return diagnostics;

  std::error_code code;
  std::filesystem::remove_all(request.outputDirectory, code);
  code.clear();
  std::filesystem::create_directories(request.outputDirectory, code);
  nlohmann::json cookedFiles = nlohmann::json::array();
  for (const auto &source : files) {
    const auto relative = std::filesystem::relative(source, projectDirectory);
    const auto target = request.outputDirectory / relative;
    std::filesystem::create_directories(target.parent_path(), code);
    if (!code)
      std::filesystem::copy_file(
          source, target, std::filesystem::copy_options::overwrite_existing,
          code);
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_COPY_FAILED",
                             .message = code.message(),
                             .path = source.string()});
      code.clear();
      continue;
    }
    cookedFiles.push_back(
        {{"path", relative.generic_string()}, {"hash", *hashFile(target)}});
  }

  nlohmann::json shaderPrograms = nlohmann::json::array();
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "Shader")
      continue;
    const auto shader = loadShaderAsset(asset.sourcePath, &diagnostics);
    if (!shader || !usesUnifiedShaderSource(*shader))
      continue;
    if (!shader->varyingDefinition) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_SHADER_VARYING_MISSING",
           .message =
               "Unified .sc shaders require a varying definition file.",
           .path = asset.sourcePath.string(),
           .suggestion =
               "Add \"varying\": \"varying.def.sc\" to the shader asset."});
      continue;
    }
    if (effectiveRequest.shaderCompiler.empty() ||
        !std::filesystem::is_regular_file(effectiveRequest.shaderCompiler)) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_SHADER_COMPILER_NOT_FOUND",
           .message = "The host bgfx shaderc executable is unavailable.",
           .path = effectiveRequest.shaderCompiler.string(),
           .suggestion = "Build the shaderc target before cooking shaders."});
      continue;
    }
    for (const ShaderVariant &variant : shaderVariants(request.platform)) {
      const auto directory =
          request.outputDirectory / "generated/shaders" /
          safeAssetName(asset.id) / variant.backend;
      const auto vertex = directory / "vertex.bin";
      const auto fragment = directory / "fragment.bin";
      if (!compileShaderStage(effectiveRequest, shader->stages.vertex,
                              *shader->varyingDefinition, vertex, "vertex",
                              variant, diagnostics) ||
          !compileShaderStage(effectiveRequest, shader->stages.fragment,
                              *shader->varyingDefinition, fragment, "fragment",
                              variant, diagnostics))
        continue;
      const auto vertexRelative =
          std::filesystem::relative(vertex, request.outputDirectory);
      const auto fragmentRelative =
          std::filesystem::relative(fragment, request.outputDirectory);
      cookedFiles.push_back({{"path", vertexRelative.generic_string()},
                             {"hash", *hashFile(vertex)}});
      cookedFiles.push_back({{"path", fragmentRelative.generic_string()},
                             {"hash", *hashFile(fragment)}});
      shaderPrograms.push_back(
          {{"asset", asset.id},
           {"backend", variant.backend},
           {"vertex", vertexRelative.generic_string()},
           {"fragment", fragmentRelative.generic_string()}});
    }
  }
  if (hasErrors(diagnostics))
    return diagnostics;
  const nlohmann::json manifest{
      {"format_version", 1},
      {"platform", request.platform},
      {"project", absoluteProject.filename().generic_string()},
      {"files", std::move(cookedFiles)},
      {"shader_programs", std::move(shaderPrograms)},
  };
  std::ofstream output(request.outputDirectory / "cook.manifest.json");
  if (!output)
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "COOK_MANIFEST_WRITE_FAILED",
                           .message = "Could not write cook manifest.",
                           .path = request.outputDirectory.string()});
  else
    output << manifest.dump(2) << '\n';
  return diagnostics;
}

} // namespace demi::assets
