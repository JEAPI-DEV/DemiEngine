#include "demi/runtime/render/backend/SvgDecoder2D.h"

#if DEMI_HAS_RSVG
#include <cairo.h>
#include <librsvg/rsvg.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace demi::runtime::render {

bool decodeSvg2D(const std::filesystem::path &path, const bool monochrome,
                 ImageData2D &image, std::string &error) {
  image = {};
#if DEMI_HAS_RSVG
  constexpr int RasterSize = 256;
  GError *svgError = nullptr;
  RsvgHandle *handle =
      rsvg_handle_new_from_file(path.string().c_str(), &svgError);
  if (handle == nullptr) {
    error = svgError != nullptr ? svgError->message
                                : "Could not open the SVG asset.";
    if (svgError != nullptr)
      g_error_free(svgError);
    return false;
  }

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, RasterSize, RasterSize);
  cairo_t *context = cairo_create(surface);
  cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(context);
  cairo_set_operator(context, CAIRO_OPERATOR_OVER);
  const RsvgRectangle viewport{
      .x = 0.0, .y = 0.0, .width = RasterSize, .height = RasterSize};
  const gboolean rendered =
      rsvg_handle_render_document(handle, context, &viewport, &svgError);
  cairo_surface_flush(surface);

  if (rendered) {
    image.width = RasterSize;
    image.height = RasterSize;
    image.rgba.resize(static_cast<std::size_t>(RasterSize * RasterSize * 4));
    const unsigned char *source = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < RasterSize; ++y) {
      for (int x = 0; x < RasterSize; ++x) {
        const unsigned char *bgra = source + y * stride + x * 4;
        auto *rgba = reinterpret_cast<unsigned char *>(image.rgba.data()) +
                     (y * RasterSize + x) * 4;
        const unsigned char alpha = bgra[3];
        const auto straight = [alpha](const unsigned char value) {
          if (alpha == 0)
            return static_cast<unsigned char>(0);
          return static_cast<unsigned char>(std::min(
              255, (static_cast<int>(value) * 255 + alpha / 2) / alpha));
        };
        rgba[0] = monochrome ? 255 : straight(bgra[2]);
        rgba[1] = monochrome ? 255 : straight(bgra[1]);
        rgba[2] = monochrome ? 255 : straight(bgra[0]);
        rgba[3] = alpha;
      }
    }
  }

  if (svgError != nullptr) {
    if (!rendered)
      error = svgError->message;
    g_error_free(svgError);
  }
  cairo_destroy(context);
  cairo_surface_destroy(surface);
  g_object_unref(handle);
  if (!rendered) {
    if (error.empty())
      error = "Could not rasterize the SVG asset.";
    return false;
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
