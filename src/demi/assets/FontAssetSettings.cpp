#include "demi/assets/FontAssetSettings.h"
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace demi::assets {
runtime::ui::FontVariations fontAssetVariations(const AssetManifest &asset) {
  const auto settings = nlohmann::json::parse(
      asset.settingsJson.empty() ? "{}" : asset.settingsJson);
  if (!settings.is_object())
    throw std::invalid_argument("Font settings must be an object");
  const auto axes = settings.value("variations", nlohmann::json::object());
  if (!axes.is_object())
    throw std::invalid_argument(
        "Font variations must be an axis-to-number object");
  runtime::ui::FontVariations result;
  for (const auto &[tag, coordinate] : axes.items()) {
    if (tag.size() != 4 || !coordinate.is_number())
      throw std::invalid_argument(
          "Font axes require four-character tags and numeric coordinates");
    const float value = coordinate.get<float>();
    if (!std::isfinite(value))
      throw std::invalid_argument("Font variation coordinates must be finite");
    result.emplace(tag, value);
  }
  return result;
}
Diagnostics validateFontAssetSettings(const AssetManifest &asset) {
  try {
    const auto variations = fontAssetVariations(asset);
    if (!std::filesystem::is_regular_file(asset.sourcePath))
      return {};
    std::ifstream file(asset.sourcePath, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
    runtime::ui::FontRasterizer rasterizer;
    std::string error;
    if (!rasterizer.open(std::as_bytes(std::span(bytes)), variations, error))
      throw std::invalid_argument(error);
  } catch (const std::exception &e) {
    return {{.severity = Severity::Error,
             .code = "FONT_VARIATIONS_INVALID",
             .message = e.what(),
             .path = asset.manifestPath.string(),
             .suggestion = "Use axes and coordinate ranges supported by this "
                           "font. Omit variations for its default instance."}};
  }
  return {};
}
} // namespace demi::assets
