#include "demi/assets/RenderAsset.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>

namespace demi::assets {
namespace {

std::optional<nlohmann::json> readDocument(const std::filesystem::path &path,
                                           Diagnostics *diagnostics,
                                           const std::string &kind) {
  try {
    std::ifstream input(path);
    if (!input) {
      if (diagnostics)
        diagnostics->push_back({.severity = Severity::Error,
                                .code = "RENDER_ASSET_SOURCE_NOT_FOUND",
                                .message = kind + " source could not be read.",
                                .path = path.string()});
      return std::nullopt;
    }
    return nlohmann::json::parse(input);
  } catch (const nlohmann::json::exception &error) {
    if (diagnostics)
      diagnostics->push_back({.severity = Severity::Error,
                              .code = "RENDER_ASSET_INVALID_JSON",
                              .message = error.what(),
                              .path = path.string()});
    return std::nullopt;
  }
}

void invalid(Diagnostics *diagnostics, const std::filesystem::path &path,
             const std::string &code, const std::string &message) {
  if (diagnostics)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = code,
                            .message = message,
                            .path = path.string()});
}

bool validColor(const nlohmann::json &value) {
  return value.is_array() && value.size() == 4 &&
         std::ranges::all_of(
             value, [](const auto &channel) { return channel.is_number(); });
}

} // namespace

std::optional<MaterialAsset>
loadMaterialAsset(const std::filesystem::path &path, Diagnostics *diagnostics) {
  const auto document = readDocument(path, diagnostics, "Material");
  if (!document || !document->is_object())
    return std::nullopt;

  try {
    MaterialAsset material;
    if (!document->contains("format_version") ||
        !(*document)["format_version"].is_number_integer()) {
      invalid(diagnostics, path, "MATERIAL_FORMAT_VERSION_MISSING",
              "Material assets require an integer format_version.");
      return std::nullopt;
    }
    material.formatVersion = (*document)["format_version"].get<int>();
    if (material.formatVersion != 1) {
      invalid(diagnostics, path, "MATERIAL_FORMAT_VERSION_UNSUPPORTED",
              "Only material format_version 1 is supported.");
      return std::nullopt;
    }
    material.shader = document->value("shader", "builtin://lit");
    material.fallback = document->value("fallback", "builtin://unlit");
    if (material.shader.empty()) {
      invalid(diagnostics, path, "MATERIAL_SHADER_MISSING",
              "Material shader must not be empty.");
      return std::nullopt;
    }

    if (const auto found = document->find("textures");
        found != document->end()) {
      if (!found->is_object()) {
        invalid(diagnostics, path, "MATERIAL_TEXTURES_INVALID",
                "Material textures must be an object of asset references.");
        return std::nullopt;
      }
      for (const auto &[slot, value] : found->items()) {
        if (!value.is_string() ||
            !value.get<std::string>().starts_with("asset://")) {
          invalid(diagnostics, path, "MATERIAL_TEXTURE_INVALID",
                  "Material texture slots require asset:// references.");
          return std::nullopt;
        }
        material.textures.emplace(slot, value.get<std::string>());
      }
    }

    if (const auto found = document->find("parameters");
        found != document->end()) {
      if (!found->is_object()) {
        invalid(diagnostics, path, "MATERIAL_PARAMETERS_INVALID",
                "Material parameters must be an object.");
        return std::nullopt;
      }
      for (const auto &[name, value] : found->items()) {
        if (value.is_number()) {
          material.numbers.emplace(name, value.get<float>());
        } else if (validColor(value)) {
          material.colors.emplace(name, runtime::Color{value[0].get<float>(),
                                                       value[1].get<float>(),
                                                       value[2].get<float>(),
                                                       value[3].get<float>()});
        } else {
          invalid(
              diagnostics, path, "MATERIAL_PARAMETER_INVALID",
              "Material parameters must be numbers or four-channel colors.");
          return std::nullopt;
        }
      }
    }

    if (const auto found = document->find("render_state");
        found != document->end()) {
      if (!found->is_object()) {
        invalid(diagnostics, path, "MATERIAL_RENDER_STATE_INVALID",
                "Material render_state must be an object.");
        return std::nullopt;
      }
      material.renderState.blend = found->value("blend", "opaque");
      material.renderState.cull = found->value("cull", "back");
      material.renderState.depthTest = found->value("depth_test", true);
      material.renderState.depthWrite = found->value("depth_write", true);
    }
    constexpr std::array<std::string_view, 3> blends{"opaque", "alpha",
                                                     "additive"};
    constexpr std::array<std::string_view, 3> culls{"back", "front", "none"};
    if (std::ranges::find(blends, material.renderState.blend) == blends.end()) {
      invalid(diagnostics, path, "MATERIAL_BLEND_INVALID",
              "Material blend must be opaque, alpha, or additive.");
      return std::nullopt;
    }
    if (std::ranges::find(culls, material.renderState.cull) == culls.end()) {
      invalid(diagnostics, path, "MATERIAL_CULL_INVALID",
              "Material cull must be back, front, or none.");
      return std::nullopt;
    }
    return material;
  } catch (const nlohmann::json::exception &error) {
    invalid(diagnostics, path, "MATERIAL_FIELD_TYPE_INVALID", error.what());
    return std::nullopt;
  }
}

std::optional<ShaderAsset> loadShaderAsset(const std::filesystem::path &path,
                                           Diagnostics *diagnostics) {
  const auto document = readDocument(path, diagnostics, "Shader");
  if (!document || !document->is_object())
    return std::nullopt;
  try {
    if (!document->contains("format_version") ||
        !(*document)["format_version"].is_number_integer() ||
        (*document)["format_version"].get<int>() != 1) {
      invalid(diagnostics, path, "SHADER_FORMAT_VERSION_UNSUPPORTED",
              "Shader assets require format_version 1.");
      return std::nullopt;
    }

    ShaderAsset shader;
    shader.formatVersion = 1;
    const std::string vertex = document->value("vertex", "");
    const std::string fragment = document->value("fragment", "");
    if (vertex.empty() || fragment.empty()) {
      invalid(diagnostics, path, "SHADER_STAGE_MISSING",
              "Shader assets require vertex and fragment source paths.");
      return std::nullopt;
    }
    shader.vertex = path.parent_path() / vertex;
    shader.fragment = path.parent_path() / fragment;
    if (!std::filesystem::is_regular_file(shader.vertex) ||
        !std::filesystem::is_regular_file(shader.fragment)) {
      invalid(diagnostics, path, "SHADER_STAGE_NOT_FOUND",
              "A shader stage source file does not exist.");
      return std::nullopt;
    }
    if (const auto fallback = document->find("platform_fallbacks");
        fallback != document->end() && fallback->is_object()) {
      shader.androidFallback = fallback->value("android", "builtin://unlit");
      shader.linuxFallback = fallback->value("linux", "builtin://unlit");
    }
    const auto validFallback = [](const std::string &value) {
      return value.starts_with("builtin://") || value.starts_with("asset://");
    };
    if (!validFallback(shader.androidFallback) ||
        !validFallback(shader.linuxFallback)) {
      invalid(diagnostics, path, "SHADER_PLATFORM_FALLBACK_INVALID",
              "Shader platform fallbacks require builtin:// or asset:// IDs.");
      return std::nullopt;
    }
    return shader;
  } catch (const nlohmann::json::exception &error) {
    invalid(diagnostics, path, "SHADER_FIELD_TYPE_INVALID", error.what());
    return std::nullopt;
  }
}

std::optional<RenderTargetAsset>
loadRenderTargetAsset(const std::filesystem::path &path,
                      Diagnostics *diagnostics) {
  const auto document = readDocument(path, diagnostics, "Render target");
  if (!document || !document->is_object())
    return std::nullopt;
  try {
    if (document->value("format_version", 0) != 1) {
      invalid(diagnostics, path, "RENDER_TARGET_FORMAT_VERSION_UNSUPPORTED",
              "Render targets require format_version 1.");
      return std::nullopt;
    }
    RenderTargetAsset target;
    target.width = document->value("width", 256);
    target.height = document->value("height", 256);
    target.format = document->value("format", "rgba8");
    target.depth = document->value("depth", true);
    if (target.width < 1 || target.width > 4096 || target.height < 1 ||
        target.height > 4096) {
      invalid(diagnostics, path, "RENDER_TARGET_SIZE_INVALID",
              "Render target width and height must be between 1 and 4096.");
      return std::nullopt;
    }
    if (target.format != "rgba8") {
      invalid(diagnostics, path, "RENDER_TARGET_FORMAT_INVALID",
              "The lightweight renderer currently supports rgba8 targets.");
      return std::nullopt;
    }
    if (!target.depth) {
      invalid(diagnostics, path, "RENDER_TARGET_DEPTH_UNSUPPORTED",
              "3D render targets currently require a depth buffer.");
      return std::nullopt;
    }
    return target;
  } catch (const nlohmann::json::exception &error) {
    invalid(diagnostics, path, "RENDER_TARGET_FIELD_TYPE_INVALID",
            error.what());
    return std::nullopt;
  }
}

} // namespace demi::assets
