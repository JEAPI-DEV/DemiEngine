#include "demi/runtime/render/backend/SvgDecoder2D.h"

#if DEMI_HAS_RSVG
#include <cairo.h>
#include <librsvg/rsvg.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace demi::runtime::render {
#if DEMI_HAS_RSVG
namespace {

constexpr int IconRasterSize = 256;

bool rasterDimensions(RsvgHandle *handle, bool monochrome, int &width,
                      int &height, std::string &error) {
  double sourceWidth = IconRasterSize;
  double sourceHeight = IconRasterSize;
  if (!monochrome && !rsvg_handle_get_intrinsic_size_in_pixels(
                         handle, &sourceWidth, &sourceHeight)) {
    gboolean hasViewBox = false;
    RsvgRectangle viewBox{};
    rsvg_handle_get_intrinsic_dimensions(handle, nullptr, nullptr, nullptr,
                                         nullptr, &hasViewBox, &viewBox);
    sourceWidth = IconRasterSize;
    sourceHeight = IconRasterSize;
    if (hasViewBox) {
      const bool validAspect = std::isfinite(viewBox.width) &&
                               std::isfinite(viewBox.height) &&
                               viewBox.width > 0.0 && viewBox.height > 0.0;
      if (!validAspect) {
        error = "SVG viewBox must have finite positive dimensions.";
        return false;
      }
      // A viewBox supplies a ratio, not a pixel resolution. Use the established
      // fallback edge while retaining that ratio in the output image itself.
      const double scale =
          IconRasterSize / std::max(viewBox.width, viewBox.height);
      sourceWidth = viewBox.width * scale;
      sourceHeight = viewBox.height * scale;
    }
  }
  const double rasterWidth = std::ceil(sourceWidth);
  const double rasterHeight = std::ceil(sourceHeight);
  constexpr auto maximum = std::numeric_limits<std::uint16_t>::max();
  if (!std::isfinite(rasterWidth) || !std::isfinite(rasterHeight) ||
      rasterWidth <= 0.0 || rasterHeight <= 0.0 || rasterWidth > maximum ||
      rasterHeight > maximum) {
    error = "2D images must be a non-empty image up to 65535px.";
    return false;
  }
  width = static_cast<int>(rasterWidth);
  height = static_cast<int>(rasterHeight);
  return true;
}

} // namespace
#endif

bool decodeSvg2D(const std::filesystem::path &path, const bool monochrome,
                 ImageData2D &image, std::string &error) {
  image = {};
  error.clear();
#if DEMI_HAS_RSVG
  GError *svgError = nullptr;
  const auto releaseHandle = [](RsvgHandle *handle) { g_object_unref(handle); };
  std::unique_ptr<RsvgHandle, decltype(releaseHandle)> handle(
      rsvg_handle_new_from_file(path.string().c_str(), &svgError),
      releaseHandle);
  if (!handle) {
    error = svgError ? svgError->message : "Could not open the SVG asset.";
    if (svgError) {
      g_error_free(svgError);
    }
    return false;
  }
  int width = 0;
  int height = 0;
  if (!rasterDimensions(handle.get(), monochrome, width, height, error)) {
    return false;
  }
  std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height),
      cairo_surface_destroy);
  const auto surfaceStatus = cairo_surface_status(surface.get());
  if (surfaceStatus != CAIRO_STATUS_SUCCESS) {
    error = cairo_status_to_string(surfaceStatus);
    return false;
  }
  std::unique_ptr<cairo_t, decltype(&cairo_destroy)> context(
      cairo_create(surface.get()), cairo_destroy);
  cairo_set_operator(context.get(), CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(context.get(), 0.0, 0.0, 0.0, 0.0);
  cairo_paint(context.get());
  cairo_set_operator(context.get(), CAIRO_OPERATOR_OVER);
  const RsvgRectangle viewport{.x = 0.0,
                               .y = 0.0,
                               .width = static_cast<double>(width),
                               .height = static_cast<double>(height)};
  const gboolean rendered = rsvg_handle_render_document(
      handle.get(), context.get(), &viewport, &svgError);
  const auto contextStatus = cairo_status(context.get());
  if (!rendered || contextStatus != CAIRO_STATUS_SUCCESS) {
    error = svgError ? svgError->message
            : contextStatus != CAIRO_STATUS_SUCCESS
                ? cairo_status_to_string(contextStatus)
                : "Could not rasterize the SVG asset.";
    if (svgError) {
      g_error_free(svgError);
    }
    return false;
  }
  if (svgError) {
    g_error_free(svgError);
  }
  cairo_surface_flush(surface.get());
  image.width = static_cast<std::uint16_t>(width);
  image.height = static_cast<std::uint16_t>(height);
  image.rgba.resize(static_cast<std::size_t>(width) * height * 4U);
  const unsigned char *source = cairo_image_surface_get_data(surface.get());
  const int stride = cairo_image_surface_get_stride(surface.get());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const unsigned char *bgra =
          source + static_cast<std::size_t>(y) * stride + x * 4;
      auto *rgba = reinterpret_cast<unsigned char *>(image.rgba.data()) +
                   (static_cast<std::size_t>(y) * width + x) * 4U;
      const unsigned char alpha = bgra[3];
      const auto straight = [alpha](const unsigned char value) {
        if (alpha == 0) {
          return static_cast<unsigned char>(0);
        }
        return static_cast<unsigned char>(
            std::min(255, (static_cast<int>(value) * 255 + alpha / 2) / alpha));
      };
      rgba[0] = monochrome ? 255 : straight(bgra[2]);
      rgba[1] = monochrome ? 255 : straight(bgra[1]);
      rgba[2] = monochrome ? 255 : straight(bgra[0]);
      rgba[3] = alpha;
    }
  }
  return true;
#else
  (void)path;
  (void)monochrome;
  error = "SVG support was not enabled in this build.";
  return false;
#endif
}

} // namespace demi::runtime::render
