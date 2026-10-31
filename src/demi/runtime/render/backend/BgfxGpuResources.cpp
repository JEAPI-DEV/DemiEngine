#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/BgfxVertexLayout.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include <fs_demi_directional_essl.h>
#include <fs_demi_directional_glsl.h>
#include <fs_demi_directional_spv.h>
#include <fs_demi_lit_essl.h>
#include <fs_demi_lit_glsl.h>
#include <fs_demi_lit_spv.h>
#include <fs_demi_pbr_directional_essl.h>
#include <fs_demi_pbr_directional_glsl.h>
#include <fs_demi_pbr_directional_spv.h>
#include <fs_demi_pbr_essl.h>
#include <fs_demi_pbr_glsl.h>
#include <fs_demi_pbr_spv.h>
#include <fs_demi_post_process_essl.h>
#include <fs_demi_post_process_glsl.h>
#include <fs_demi_post_process_spv.h>
#include <fs_drawstress.bin.h>
#include <fs_drawstress_tex.bin.h>
#include <fs_ocornut_imgui.bin.h>
#include <vs_demi_lit_essl.h>
#include <vs_demi_lit_glsl.h>
#include <vs_demi_lit_instanced_essl.h>
#include <vs_demi_lit_instanced_glsl.h>
#include <vs_demi_lit_instanced_spv.h>
#include <vs_demi_lit_spv.h>
#include <vs_demi_post_process_essl.h>
#include <vs_demi_post_process_glsl.h>
#include <vs_demi_post_process_spv.h>
#include <vs_demi_skinned_spv.h>
#include <vs_drawstress.bin.h>
#include <vs_drawstress_tex.bin.h>
#include <vs_ocornut_imgui.bin.h>

#include <algorithm>
#include <array>
#include <fs_demi_sky_essl.h>
#include <fs_demi_sky_glsl.h>
#include <fs_demi_sky_spv.h>
#include <limits>
#include <vs_demi_sky_essl.h>
#include <vs_demi_sky_glsl.h>
#include <vs_demi_sky_spv.h>

#include <fs_demi_terrain_directional_essl.h>
#include <fs_demi_terrain_directional_glsl.h>
#include <fs_demi_terrain_directional_spv.h>
#include <fs_demi_terrain_essl.h>
#include <fs_demi_terrain_glsl.h>
#include <fs_demi_terrain_mapped_directional_essl.h>
#include <fs_demi_terrain_mapped_directional_glsl.h>
#include <fs_demi_terrain_mapped_directional_spv.h>
#include <fs_demi_terrain_mapped_essl.h>
#include <fs_demi_terrain_mapped_glsl.h>
#include <fs_demi_terrain_mapped_opaque_directional_essl.h>
#include <fs_demi_terrain_mapped_opaque_directional_glsl.h>
#include <fs_demi_terrain_mapped_opaque_directional_spv.h>
#include <fs_demi_terrain_mapped_opaque_essl.h>
#include <fs_demi_terrain_mapped_opaque_glsl.h>
#include <fs_demi_terrain_mapped_opaque_spv.h>
#include <fs_demi_terrain_mapped_spv.h>
#include <fs_demi_terrain_spv.h>
#include <fs_demi_water_essl.h>
#include <fs_demi_water_glsl.h>
#include <fs_demi_water_spv.h>
#include <vs_demi_terrain_essl.h>
#include <vs_demi_terrain_glsl.h>
#include <vs_demi_terrain_spv.h>
#include <vs_demi_water_essl.h>
#include <vs_demi_water_glsl.h>
#include <vs_demi_water_spv.h>

#include <fs_demi_directional_mrt_essl.h>
#include <fs_demi_directional_mrt_glsl.h>
#include <fs_demi_directional_mrt_spv.h>
#include <fs_demi_linear_depth_resolve_essl.h>
#include <fs_demi_linear_depth_resolve_glsl.h>
#include <fs_demi_linear_depth_resolve_spv.h>
#include <fs_demi_lit_mrt_essl.h>
#include <fs_demi_lit_mrt_glsl.h>
#include <fs_demi_lit_mrt_spv.h>
#include <fs_demi_pbr_directional_mrt_essl.h>
#include <fs_demi_pbr_directional_mrt_glsl.h>
#include <fs_demi_pbr_directional_mrt_spv.h>
#include <fs_demi_pbr_mrt_essl.h>
#include <fs_demi_pbr_mrt_glsl.h>
#include <fs_demi_pbr_mrt_spv.h>
#include <fs_demi_sky_mrt_essl.h>
#include <fs_demi_sky_mrt_glsl.h>
#include <fs_demi_sky_mrt_spv.h>
#include <fs_demi_terrain_directional_mrt_essl.h>
#include <fs_demi_terrain_directional_mrt_glsl.h>
#include <fs_demi_terrain_directional_mrt_spv.h>
#include <fs_demi_terrain_mapped_directional_mrt_essl.h>
#include <fs_demi_terrain_mapped_directional_mrt_glsl.h>
#include <fs_demi_terrain_mapped_directional_mrt_spv.h>
#include <fs_demi_terrain_mapped_mrt_essl.h>
#include <fs_demi_terrain_mapped_mrt_glsl.h>
#include <fs_demi_terrain_mapped_mrt_spv.h>
#include <fs_demi_terrain_mapped_opaque_directional_mrt_essl.h>
#include <fs_demi_terrain_mapped_opaque_directional_mrt_glsl.h>
#include <fs_demi_terrain_mapped_opaque_directional_mrt_spv.h>
#include <fs_demi_terrain_mapped_opaque_mrt_essl.h>
#include <fs_demi_terrain_mapped_opaque_mrt_glsl.h>
#include <fs_demi_terrain_mapped_opaque_mrt_spv.h>
#include <fs_demi_terrain_mrt_essl.h>
#include <fs_demi_terrain_mrt_glsl.h>
#include <fs_demi_terrain_mrt_spv.h>

#include <vs_demi_lit_instanced_mrt_essl.h>
#include <vs_demi_lit_instanced_mrt_glsl.h>
#include <vs_demi_lit_instanced_mrt_spv.h>
#include <vs_demi_lit_mrt_essl.h>
#include <vs_demi_lit_mrt_glsl.h>
#include <vs_demi_lit_mrt_spv.h>
#include <vs_demi_skinned_mrt_essl.h>
#include <vs_demi_skinned_mrt_glsl.h>
#include <vs_demi_skinned_mrt_spv.h>
#include <vs_demi_sky_mrt_essl.h>
#include <vs_demi_sky_mrt_glsl.h>
#include <vs_demi_sky_mrt_spv.h>

namespace demi::runtime::render {
namespace {

constexpr std::uint16_t Invalid = std::numeric_limits<std::uint16_t>::max();

const bgfx::EmbeddedShader EmbeddedShaders[] = {
    {"vs_demi_water",
     {{bgfx::RendererType::OpenGLES, vs_demi_water_essl,
       sizeof(vs_demi_water_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_water_glsl,
       sizeof(vs_demi_water_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_water_spv,
       sizeof(vs_demi_water_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_water",
     {{bgfx::RendererType::OpenGLES, fs_demi_water_essl,
       sizeof(fs_demi_water_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_water_glsl,
       sizeof(fs_demi_water_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_water_spv,
       sizeof(fs_demi_water_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_terrain",
     {{bgfx::RendererType::OpenGLES, vs_demi_terrain_essl,
       sizeof(vs_demi_terrain_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_terrain_glsl,
       sizeof(vs_demi_terrain_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_terrain_spv,
       sizeof(vs_demi_terrain_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_essl,
       sizeof(fs_demi_terrain_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_glsl,
       sizeof(fs_demi_terrain_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_spv,
       sizeof(fs_demi_terrain_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_directional",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_directional_essl,
       sizeof(fs_demi_terrain_directional_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_directional_glsl,
       sizeof(fs_demi_terrain_directional_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_directional_spv,
       sizeof(fs_demi_terrain_directional_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mapped_essl,
       sizeof(fs_demi_terrain_mapped_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_glsl,
       sizeof(fs_demi_terrain_mapped_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_spv,
       sizeof(fs_demi_terrain_mapped_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_directional",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mapped_directional_essl,
       sizeof(fs_demi_terrain_mapped_directional_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_directional_glsl,
       sizeof(fs_demi_terrain_mapped_directional_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_directional_spv,
       sizeof(fs_demi_terrain_mapped_directional_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_opaque",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mapped_opaque_essl,
       sizeof(fs_demi_terrain_mapped_opaque_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_opaque_glsl,
       sizeof(fs_demi_terrain_mapped_opaque_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_opaque_spv,
       sizeof(fs_demi_terrain_mapped_opaque_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_opaque_directional",
     {{bgfx::RendererType::OpenGLES,
       fs_demi_terrain_mapped_opaque_directional_essl,
       sizeof(fs_demi_terrain_mapped_opaque_directional_essl)},
      {bgfx::RendererType::OpenGL,
       fs_demi_terrain_mapped_opaque_directional_glsl,
       sizeof(fs_demi_terrain_mapped_opaque_directional_glsl)},
      {bgfx::RendererType::Vulkan,
       fs_demi_terrain_mapped_opaque_directional_spv,
       sizeof(fs_demi_terrain_mapped_opaque_directional_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_sky",
     {{bgfx::RendererType::OpenGLES, vs_demi_sky_essl,
       sizeof(vs_demi_sky_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_sky_glsl, sizeof(vs_demi_sky_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_sky_spv, sizeof(vs_demi_sky_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_sky",
     {{bgfx::RendererType::OpenGLES, fs_demi_sky_essl,
       sizeof(fs_demi_sky_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_sky_glsl, sizeof(fs_demi_sky_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_sky_spv, sizeof(fs_demi_sky_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_directional",
     {{bgfx::RendererType::OpenGLES, fs_demi_directional_essl,
       sizeof(fs_demi_directional_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_directional_glsl,
       sizeof(fs_demi_directional_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_directional_spv,
       sizeof(fs_demi_directional_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_skinned",
     {{bgfx::RendererType::Vulkan, vs_demi_skinned_spv,
       sizeof(vs_demi_skinned_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(vs_drawstress),
    BGFX_EMBEDDED_SHADER(fs_drawstress),
    BGFX_EMBEDDED_SHADER(vs_drawstress_tex),
    BGFX_EMBEDDED_SHADER(fs_drawstress_tex),
    {"vs_demi_lit",
     {{bgfx::RendererType::OpenGLES, vs_demi_lit_essl,
       sizeof(vs_demi_lit_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_lit_glsl, sizeof(vs_demi_lit_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_lit_spv, sizeof(vs_demi_lit_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_lit",
     {{bgfx::RendererType::OpenGLES, fs_demi_lit_essl,
       sizeof(fs_demi_lit_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_lit_glsl, sizeof(fs_demi_lit_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_lit_spv, sizeof(fs_demi_lit_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_pbr",
     {{bgfx::RendererType::OpenGLES, fs_demi_pbr_essl,
       sizeof(fs_demi_pbr_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_pbr_glsl, sizeof(fs_demi_pbr_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_pbr_spv, sizeof(fs_demi_pbr_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_pbr_directional",
     {{bgfx::RendererType::OpenGLES, fs_demi_pbr_directional_essl,
       sizeof(fs_demi_pbr_directional_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_pbr_directional_glsl,
       sizeof(fs_demi_pbr_directional_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_pbr_directional_spv,
       sizeof(fs_demi_pbr_directional_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_lit_instanced",
     {{bgfx::RendererType::OpenGLES, vs_demi_lit_instanced_essl,
       sizeof(vs_demi_lit_instanced_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_lit_instanced_glsl,
       sizeof(vs_demi_lit_instanced_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_lit_instanced_spv,
       sizeof(vs_demi_lit_instanced_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_post_process",
     {{bgfx::RendererType::OpenGLES, vs_demi_post_process_essl,
       sizeof(vs_demi_post_process_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_post_process_glsl,
       sizeof(vs_demi_post_process_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_post_process_spv,
       sizeof(vs_demi_post_process_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_post_process",
     {{bgfx::RendererType::OpenGLES, fs_demi_post_process_essl,
       sizeof(fs_demi_post_process_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_post_process_glsl,
       sizeof(fs_demi_post_process_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_post_process_spv,
       sizeof(fs_demi_post_process_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_lit_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_lit_mrt_essl,
       sizeof(fs_demi_lit_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_lit_mrt_glsl,
       sizeof(fs_demi_lit_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_lit_mrt_spv,
       sizeof(fs_demi_lit_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_directional_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_directional_mrt_essl,
       sizeof(fs_demi_directional_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_directional_mrt_glsl,
       sizeof(fs_demi_directional_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_directional_mrt_spv,
       sizeof(fs_demi_directional_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_pbr_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_pbr_mrt_essl,
       sizeof(fs_demi_pbr_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_pbr_mrt_glsl,
       sizeof(fs_demi_pbr_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_pbr_mrt_spv,
       sizeof(fs_demi_pbr_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_pbr_directional_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_pbr_directional_mrt_essl,
       sizeof(fs_demi_pbr_directional_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_pbr_directional_mrt_glsl,
       sizeof(fs_demi_pbr_directional_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_pbr_directional_mrt_spv,
       sizeof(fs_demi_pbr_directional_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mrt_essl,
       sizeof(fs_demi_terrain_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mrt_glsl,
       sizeof(fs_demi_terrain_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mrt_spv,
       sizeof(fs_demi_terrain_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_directional_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_directional_mrt_essl,
       sizeof(fs_demi_terrain_directional_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_directional_mrt_glsl,
       sizeof(fs_demi_terrain_directional_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_directional_mrt_spv,
       sizeof(fs_demi_terrain_directional_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mapped_mrt_essl,
       sizeof(fs_demi_terrain_mapped_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_mrt_glsl,
       sizeof(fs_demi_terrain_mapped_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_mrt_spv,
       sizeof(fs_demi_terrain_mapped_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_directional_mrt",
     {{bgfx::RendererType::OpenGLES,
       fs_demi_terrain_mapped_directional_mrt_essl,
       sizeof(fs_demi_terrain_mapped_directional_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_directional_mrt_glsl,
       sizeof(fs_demi_terrain_mapped_directional_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_directional_mrt_spv,
       sizeof(fs_demi_terrain_mapped_directional_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_opaque_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_terrain_mapped_opaque_mrt_essl,
       sizeof(fs_demi_terrain_mapped_opaque_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_terrain_mapped_opaque_mrt_glsl,
       sizeof(fs_demi_terrain_mapped_opaque_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_terrain_mapped_opaque_mrt_spv,
       sizeof(fs_demi_terrain_mapped_opaque_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_terrain_mapped_opaque_directional_mrt",
     {{bgfx::RendererType::OpenGLES,
       fs_demi_terrain_mapped_opaque_directional_mrt_essl,
       sizeof(fs_demi_terrain_mapped_opaque_directional_mrt_essl)},
      {bgfx::RendererType::OpenGL,
       fs_demi_terrain_mapped_opaque_directional_mrt_glsl,
       sizeof(fs_demi_terrain_mapped_opaque_directional_mrt_glsl)},
      {bgfx::RendererType::Vulkan,
       fs_demi_terrain_mapped_opaque_directional_mrt_spv,
       sizeof(fs_demi_terrain_mapped_opaque_directional_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_sky_mrt",
     {{bgfx::RendererType::OpenGLES, fs_demi_sky_mrt_essl,
       sizeof(fs_demi_sky_mrt_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_sky_mrt_glsl,
       sizeof(fs_demi_sky_mrt_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_sky_mrt_spv,
       sizeof(fs_demi_sky_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_demi_linear_depth_resolve",
     {{bgfx::RendererType::OpenGLES, fs_demi_linear_depth_resolve_essl,
       sizeof(fs_demi_linear_depth_resolve_essl)},
      {bgfx::RendererType::OpenGL, fs_demi_linear_depth_resolve_glsl,
       sizeof(fs_demi_linear_depth_resolve_glsl)},
      {bgfx::RendererType::Vulkan, fs_demi_linear_depth_resolve_spv,
       sizeof(fs_demi_linear_depth_resolve_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("FSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_lit_mrt",
     {{bgfx::RendererType::OpenGLES, vs_demi_lit_mrt_essl,
       sizeof(vs_demi_lit_mrt_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_lit_mrt_glsl,
       sizeof(vs_demi_lit_mrt_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_lit_mrt_spv,
       sizeof(vs_demi_lit_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_lit_instanced_mrt",
     {{bgfx::RendererType::OpenGLES, vs_demi_lit_instanced_mrt_essl,
       sizeof(vs_demi_lit_instanced_mrt_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_lit_instanced_mrt_glsl,
       sizeof(vs_demi_lit_instanced_mrt_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_lit_instanced_mrt_spv,
       sizeof(vs_demi_lit_instanced_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_skinned_mrt",
     {{bgfx::RendererType::OpenGLES, vs_demi_skinned_mrt_essl,
       sizeof(vs_demi_skinned_mrt_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_skinned_mrt_glsl,
       sizeof(vs_demi_skinned_mrt_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_skinned_mrt_spv,
       sizeof(vs_demi_skinned_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_demi_sky_mrt",
     {{bgfx::RendererType::OpenGLES, vs_demi_sky_mrt_essl,
       sizeof(vs_demi_sky_mrt_essl)},
      {bgfx::RendererType::OpenGL, vs_demi_sky_mrt_glsl,
       sizeof(vs_demi_sky_mrt_glsl)},
      {bgfx::RendererType::Vulkan, vs_demi_sky_mrt_spv,
       sizeof(vs_demi_sky_mrt_spv)},
      {bgfx::RendererType::Noop,
       reinterpret_cast<const std::uint8_t *>("VSH\x5\x0\x0\x0\x0\x0\x0"), 10},
      {bgfx::RendererType::Count, nullptr, 0}}},
    BGFX_EMBEDDED_SHADER_END(),
};

bgfx::TextureFormat::Enum textureFormat(const TextureFormat format) {
  switch (format) {
  case TextureFormat::RGBA32F:
    return bgfx::TextureFormat::RGBA32F;
  case TextureFormat::R32F:
    return bgfx::TextureFormat::R32F;
  case TextureFormat::R8:
    return bgfx::TextureFormat::R8;
  case TextureFormat::RGBA8:
    return bgfx::TextureFormat::RGBA8;
  case TextureFormat::BGRA8:
    return bgfx::TextureFormat::BGRA8;
  }
  return bgfx::TextureFormat::RGBA8;
}

std::uint64_t renderTargetFlags(int samples) {
  switch (samples) {
  case 0:
  case 1:
    return BGFX_TEXTURE_RT;
  case 2:
    return BGFX_TEXTURE_RT_MSAA_X2;
  case 4:
    return BGFX_TEXTURE_RT_MSAA_X4;
  case 8:
    return BGFX_TEXTURE_RT_MSAA_X8;
  case 16:
    return BGFX_TEXTURE_RT_MSAA_X16;
  default:
    return 0;
  }
}

std::uint64_t textureFlags(const TextureCreateInfo &info) {
  std::uint64_t flags = info.renderTarget ? renderTargetFlags(info.msaaSamples)
                                          : BGFX_TEXTURE_NONE;
  if (info.sampleMultisample) {
    flags |= BGFX_TEXTURE_MSAA_SAMPLE;
  }
  if (info.blitDestination) {
    flags |= BGFX_TEXTURE_BLIT_DST;
  }
  if (info.srgb) {
    flags |= BGFX_TEXTURE_SRGB;
  }
  if (info.minFilter.value_or(info.filter) == TextureFilter::Nearest) {
    flags |= BGFX_SAMPLER_MIN_POINT;
  }
  if (info.magFilter.value_or(info.filter) == TextureFilter::Nearest) {
    flags |= BGFX_SAMPLER_MAG_POINT;
  }
  if (info.mipFilter.value_or(info.filter) == TextureFilter::Nearest) {
    flags |= BGFX_SAMPLER_MIP_POINT;
  }
  if (info.anisotropic) {
    flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
  }
  const auto wrapFlags = [](TextureWrap mode, std::uint64_t clamp,
                            std::uint64_t mirror) {
    return mode == TextureWrap::Clamp    ? clamp
           : mode == TextureWrap::Mirror ? mirror
                                         : std::uint64_t{0};
  };
  flags |= wrapFlags(info.wrap, BGFX_SAMPLER_U_CLAMP, BGFX_SAMPLER_U_MIRROR);
  flags |= wrapFlags(info.wrapV.value_or(info.wrap), BGFX_SAMPLER_V_CLAMP,
                     BGFX_SAMPLER_V_MIRROR);
  flags |= wrapFlags(info.wrap, BGFX_SAMPLER_W_CLAMP, BGFX_SAMPLER_W_MIRROR);
  return flags;
}

const bgfx::Memory *copy(std::span<const std::byte> bytes) {
  return bytes.empty() ? nullptr
                       : bgfx::copy(bytes.data(),
                                    static_cast<std::uint32_t>(bytes.size()));
}

struct BufferValue {
  std::uint16_t index = Invalid;
  BufferKind kind = BufferKind::Vertex;
};

struct TextureRecord {
  std::uint16_t index = Invalid;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  unsigned bytesPerPixel = 0;
  std::uint16_t layers = 1;
  bool mipmaps = false;
};

unsigned texturePixelBytes(TextureFormat format) {
  switch (format) {
  case TextureFormat::R8:
    return 1;
  case TextureFormat::RGBA8:
  case TextureFormat::BGRA8:
  case TextureFormat::R32F:
    return 4;
  case TextureFormat::RGBA32F:
    return 16;
  }
  return 0;
}

std::size_t textureDataBytes(const TextureCreateInfo &info) {
  std::size_t total = 0;
  std::size_t width = info.width;
  std::size_t height = info.height;
  const auto pixelBytes = texturePixelBytes(info.format);
  while (true) {
    total += width * height * pixelBytes;
    if (!info.generateMipmaps || (width == 1 && height == 1)) {
      return total * info.layers;
    }
    width = std::max<std::size_t>(width / 2, 1);
    height = std::max<std::size_t>(height / 2, 1);
  }
}

class BgfxGpuResources final : public GpuResources, public BgfxResourceLookup {
public:
  ~BgfxGpuResources() override { clear(); }
  RenderDeviceLimits limits() const override {
    const auto *caps = bgfx::getCaps();
    if (caps == nullptr) {
      return {};
    }
    return {.maxTextureSize = caps->limits.maxTextureSize,
            .maxViews = caps->limits.maxViews,
            .originBottomLeft = caps->originBottomLeft,
            .maxTextureSamplers = caps->limits.maxTextureSamplers,
            .supportsInstancing = (caps->supported & BGFX_CAPS_INSTANCING) != 0,
            .maxTextureLayers = caps->limits.maxTextureLayers,
            .supportsTextureBlit =
                (caps->supported & BGFX_CAPS_TEXTURE_BLIT) != 0,
            .maxFrameBufferAttachments = caps->limits.maxFBAttachments,
            .supportsMultisampleCountQuery =
                bgfx::getRendererType() == bgfx::RendererType::Vulkan};
  }

  TextureCapabilities textureCapabilities(TextureFormat format) const override {
    const auto *caps = bgfx::getCaps();
    if (caps == nullptr || texturePixelBytes(format) == 0) {
      return {};
    }
    const auto supported = caps->formats[textureFormat(format)];
    return {
        .sampled2D = (supported & (BGFX_CAPS_FORMAT_TEXTURE_2D |
                                   BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED)) != 0,
        .vertexSampling = (supported & BGFX_CAPS_FORMAT_TEXTURE_VERTEX) != 0,
        .sampledArray =
            (caps->supported & BGFX_CAPS_TEXTURE_2D_ARRAY) != 0 &&
            (supported & (BGFX_CAPS_FORMAT_TEXTURE_2D |
                          BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED)) != 0,
        .renderTarget = (supported & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0,
        .multisampleRenderTarget =
            (supported & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA) != 0,
        .sampledMultisample = (supported & BGFX_CAPS_FORMAT_TEXTURE_MSAA) != 0};
  }

  std::string_view shaderBackend() const override {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Vulkan:
      return "vulkan";
    case bgfx::RendererType::OpenGLES:
      return "opengles";
    case bgfx::RendererType::OpenGL:
      return "opengl";
    case bgfx::RendererType::Noop:
      return "noop";
    default:
      return {};
    }
  }

  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    if (!renderTargetFlags(info.msaaSamples) ||
        (info.msaaSamples > 1 &&
         (!info.renderTarget || info.generateMipmaps || !info.data.empty()))) {
      error = "MSAA requires a render target and 1, 2, 4, 8 or 16 samples (0 "
              "disables it).";
      return {};
    }
    if (info.sampleMultisample &&
        (!info.renderTarget || info.msaaSamples <= 1 ||
         !textureCapabilities(info.format).sampledMultisample)) {
      error = "Multisample sampling requires a supported multisample render "
              "target.";
      return {};
    }
    if (info.srgb && info.format != TextureFormat::RGBA8) {
      error = "sRGB texture sampling requires RGBA8 color storage.";
      return {};
    }
    if (info.layers == 0 || info.layers > limits().maxTextureLayers ||
        (info.layers > 1 && (!textureCapabilities(info.format).sampledArray ||
                             info.renderTarget))) {
      error = "Texture array layers exceed device support or require an "
              "unsupported render target.";
      return {};
    }
    if (info.width == 0 || info.height == 0) {
      error = "Texture dimensions must be positive.";
      return {};
    }
    if (info.width > limits().maxTextureSize ||
        info.height > limits().maxTextureSize) {
      error = "Texture dimensions exceed the graphics device limit.";
      return {};
    }
    if (info.data.size() > UINT32_MAX || texturePixelBytes(info.format) == 0 ||
        (!info.data.empty() && info.data.size() != textureDataBytes(info))) {
      error =
          "Texture initial data must match its format and complete mip layout.";
      return {};
    }
    if (!bgfx::isTextureValid(0, false, info.layers, textureFormat(info.format),
                              textureFlags(info))) {
      error = "Texture format or MSAA mode is unsupported";
      return {};
    }
    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        info.width, info.height, info.generateMipmaps, info.layers,
        textureFormat(info.format), textureFlags(info), copy(info.data));
    if (!bgfx::isValid(texture)) {
      error = "bgfx could not create texture " + info.debugName + ".";
      return {};
    }
    if (!info.debugName.empty())
      bgfx::setName(texture, info.debugName.c_str());
    return textures_.insert({texture.idx, info.width, info.height,
                             info.renderTarget || !info.data.empty()
                                 ? 0U
                                 : texturePixelBytes(info.format),
                             info.layers, info.generateMipmaps});
  }

  bool updateTexture(const TextureHandle handle, const TextureUpdateInfo &info,
                     std::string &error) override {
    const auto *texture = textures_.find(handle);
    if (!texture || texture->bytesPerPixel == 0 || info.width == 0 ||
        info.height == 0 || info.layer >= texture->layers ||
        (!texture->mipmaps && info.mip != 0)) {
      error = "Texture update handle, layer or mip level is invalid.";
      return false;
    }
    unsigned mipWidth = texture->width;
    unsigned mipHeight = texture->height;
    for (unsigned mip = 0; mip < info.mip; ++mip) {
      if (mipWidth == 1 && mipHeight == 1) {
        error = "Texture update mip level exceeds the complete chain.";
        return false;
      }
      mipWidth = std::max(1U, mipWidth / 2);
      mipHeight = std::max(1U, mipHeight / 2);
    }
    const auto expectedBytes = static_cast<std::size_t>(info.width) *
                               info.height * texture->bytesPerPixel;
    if (unsigned(info.x) + info.width > mipWidth ||
        unsigned(info.y) + info.height > mipHeight ||
        info.data.size() > UINT32_MAX || info.data.size() != expectedBytes) {
      error = "Texture update region or format-sized data is invalid.";
      return false;
    }
    bgfx::updateTexture2D(bgfx::TextureHandle{texture->index}, info.layer,
                          info.mip, info.x, info.y, info.width, info.height,
                          copy(info.data));
    error.clear();
    return true;
  }

  SamplerHandle createSampler(const std::string_view name,
                              std::string &error) override {
    if (name.empty()) {
      error = "Sampler names must not be empty.";
      return {};
    }
    const bgfx::UniformHandle sampler = bgfx::createUniform(
        std::string(name).c_str(), bgfx::UniformType::Sampler);
    if (!bgfx::isValid(sampler)) {
      error = "bgfx could not create sampler " + std::string(name) + ".";
      return {};
    }
    return samplers_.insert(sampler.idx);
  }

  UniformHandle createUniform(const std::string_view name,
                              const UniformType type, const std::uint16_t count,
                              std::string &error) override {
    if (name.empty() || count == 0) {
      error = "Uniform names and element counts must be valid.";
      return {};
    }
    bgfx::UniformType::Enum nativeType = bgfx::UniformType::Vec4;
    if (type == UniformType::Matrix3)
      nativeType = bgfx::UniformType::Mat3;
    else if (type == UniformType::Matrix4)
      nativeType = bgfx::UniformType::Mat4;
    const bgfx::UniformHandle uniform =
        bgfx::createUniform(std::string(name).c_str(), nativeType, count);
    if (!bgfx::isValid(uniform)) {
      error = "bgfx could not create uniform " + std::string(name) + ".";
      return {};
    }
    return uniforms_.insert(uniform.idx);
  }

  BufferHandle createBuffer(const BufferCreateInfo &info,
                            std::string &error) override {
    if (info.data.empty()) {
      error = "GPU buffer data must not be empty.";
      return {};
    }
    BufferValue value{.kind = info.kind};
    if (info.kind == BufferKind::Vertex ||
        info.kind == BufferKind::DynamicVertex) {
      bgfx::VertexLayout layout;
      if (!buildBgfxVertexLayout(info.vertexLayout, layout, error))
        return {};
      if (info.data.size() % layout.getStride() != 0) {
        error = "Vertex buffer byte count must be a multiple of its layout "
                "stride.";
        return {};
      }
      if (info.kind == BufferKind::DynamicVertex) {
        const auto count =
            static_cast<std::uint32_t>(info.data.size() / layout.getStride());
        const bgfx::DynamicVertexBufferHandle buffer =
            bgfx::createDynamicVertexBuffer(count, layout);
        if (bgfx::isValid(buffer)) {
          bgfx::update(buffer, 0, copy(info.data));
          value.index = buffer.idx;
        }
      } else {
        const bgfx::VertexBufferHandle buffer =
            bgfx::createVertexBuffer(copy(info.data), layout);
        if (bgfx::isValid(buffer))
          value.index = buffer.idx;
      }
    } else {
      const std::uint16_t flags =
          info.kind == BufferKind::Index32 ? BGFX_BUFFER_INDEX32 : 0;
      const bgfx::IndexBufferHandle buffer =
          bgfx::createIndexBuffer(copy(info.data), flags);
      if (bgfx::isValid(buffer))
        value.index = buffer.idx;
    }
    if (value.index == Invalid) {
      error = "bgfx could not create buffer " + info.debugName + ".";
      return {};
    }
    return buffers_.insert(value);
  }

  bool updateBuffer(const BufferHandle handle,
                    const std::span<const std::byte> data,
                    std::string &error) override {
    const BufferValue *value = buffers_.find(handle);
    if (value == nullptr || value->kind != BufferKind::DynamicVertex ||
        data.empty()) {
      error = "Dynamic vertex buffer update is invalid.";
      return false;
    }
    bgfx::update(bgfx::DynamicVertexBufferHandle{value->index}, 0, copy(data));
    error.clear();
    return true;
  }

  ProgramHandle createProgram(const ProgramCreateInfo &info,
                              std::string &error) override {
    if (info.vertexShader.empty() || info.fragmentShader.empty()) {
      error = "A GPU program requires cooked vertex and fragment shaders.";
      return {};
    }
    const bgfx::ShaderHandle vertex =
        bgfx::createShader(copy(info.vertexShader));
    const bgfx::ShaderHandle fragment =
        bgfx::createShader(copy(info.fragmentShader));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
      if (bgfx::isValid(vertex))
        bgfx::destroy(vertex);
      if (bgfx::isValid(fragment))
        bgfx::destroy(fragment);
      error = "bgfx rejected cooked shader data for " + info.debugName + ".";
      return {};
    }
    const bgfx::ProgramHandle program =
        bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(program)) {
      error = "bgfx could not link program " + info.debugName + ".";
      return {};
    }
    return programs_.insert(program.idx);
  }

  ProgramHandle createBuiltinProgram(const BuiltinProgram program,
                                     std::string &error) override {
    return createBuiltinProgram(program, false, error);
  }

  ProgramHandle createBuiltinProgram(const BuiltinProgram program,
                                     bool linearDepthOutput,
                                     std::string &error) override {
    const char *vertexName = nullptr;
    const char *fragmentName = nullptr;
    switch (program) {
    case BuiltinProgram::Textured2D:
      vertexName = "vs_ocornut_imgui";
      fragmentName = "fs_ocornut_imgui";
      break;
    case BuiltinProgram::VertexColor3D:
      vertexName = "vs_drawstress";
      fragmentName = "fs_drawstress";
      break;
    case BuiltinProgram::Textured3D:
      vertexName = "vs_drawstress_tex";
      fragmentName = "fs_drawstress_tex";
      break;
    case BuiltinProgram::Pbr3D:
      vertexName = "vs_demi_lit";
      fragmentName = "fs_demi_pbr";
      break;
    case BuiltinProgram::Pbr3DDirectional:
      vertexName = "vs_demi_lit";
      fragmentName = "fs_demi_pbr_directional";
      break;
    case BuiltinProgram::Pbr3DInstanced:
      vertexName = "vs_demi_lit_instanced";
      fragmentName = "fs_demi_pbr";
      break;
    case BuiltinProgram::Pbr3DDirectionalInstanced:
      vertexName = "vs_demi_lit_instanced";
      fragmentName = "fs_demi_pbr_directional";
      break;
    case BuiltinProgram::Pbr3DSkinned:
      vertexName = "vs_demi_skinned";
      fragmentName = "fs_demi_pbr";
      break;
    case BuiltinProgram::Pbr3DDirectionalSkinned:
      vertexName = "vs_demi_skinned";
      fragmentName = "fs_demi_pbr_directional";
      break;
    case BuiltinProgram::Lit3D:
      vertexName = "vs_demi_lit";
      fragmentName = "fs_demi_lit";
      break;
    case BuiltinProgram::Sky3D:
      vertexName = "vs_demi_sky";
      fragmentName = "fs_demi_sky";
      break;
    case BuiltinProgram::WaterSurface3D:
      vertexName = "vs_demi_water";
      fragmentName = "fs_demi_water";
      break;
    case BuiltinProgram::Lit3DInstanced:
      vertexName = "vs_demi_lit_instanced";
      fragmentName = "fs_demi_lit";
      break;
    case BuiltinProgram::Lit3DSkinned:
      vertexName = "vs_demi_skinned";
      fragmentName = "fs_demi_lit";
      break;
    case BuiltinProgram::Directional3D:
      vertexName = "vs_demi_lit";
      fragmentName = "fs_demi_directional";
      break;
    case BuiltinProgram::Directional3DInstanced:
      vertexName = "vs_demi_lit_instanced";
      fragmentName = "fs_demi_directional";
      break;
    case BuiltinProgram::Directional3DSkinned:
      vertexName = "vs_demi_skinned";
      fragmentName = "fs_demi_directional";
      break;
    case BuiltinProgram::LinearDepthResolve3D:
      vertexName = "vs_demi_post_process";
      fragmentName = "fs_demi_linear_depth_resolve";
      break;
    case BuiltinProgram::PostProcess2D:
      vertexName = "vs_demi_post_process";
      fragmentName = "fs_demi_post_process";
      break;
    case BuiltinProgram::Terrain3D:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain";
      break;
    case BuiltinProgram::TerrainMapped3D:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain_mapped";
      break;
    case BuiltinProgram::TerrainMapped3DDirectional:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain_mapped_directional";
      break;
    case BuiltinProgram::TerrainMappedOpaque3D:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain_mapped_opaque";
      break;
    case BuiltinProgram::TerrainMappedOpaque3DDirectional:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain_mapped_opaque_directional";
      break;
    case BuiltinProgram::Terrain3DDirectional:
      vertexName = "vs_demi_terrain";
      fragmentName = "fs_demi_terrain_directional";
      break;
    }
    std::string outputVariant;
    std::string vertexVariant;
    if (linearDepthOutput) {
      const std::string_view fragment(fragmentName);
      const bool supportsAnalyticDepth =
          fragment == "fs_demi_lit" || fragment == "fs_demi_directional" ||
          fragment.starts_with("fs_demi_pbr") ||
          fragment.starts_with("fs_demi_terrain") || fragment == "fs_demi_sky";
      if (!supportsAnalyticDepth) {
        error = "This builtin program does not support analytic depth output.";
        return {};
      }
      outputVariant = std::string(fragment) + "_mrt";
      fragmentName = outputVariant.c_str();
      if (std::string_view(vertexName) != "vs_demi_terrain") {
        vertexVariant = std::string(vertexName) + "_mrt";
        vertexName = vertexVariant.c_str();
      }
    }
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertex =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, vertexName);
    const bgfx::ShaderHandle fragment =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, fragmentName);
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
      if (bgfx::isValid(vertex))
        bgfx::destroy(vertex);
      if (bgfx::isValid(fragment))
        bgfx::destroy(fragment);
      error = "bgfx could not create the requested built-in program.";
      return {};
    }
    const bgfx::ProgramHandle handle =
        bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(handle)) {
      error = "bgfx could not link the requested built-in program.";
      return {};
    }
    return programs_.insert(handle.idx);
  }

  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &info,
                                         std::string &error) override {
    if (!renderTargetFlags(info.msaaSamples) || info.width == 0 ||
        info.height == 0) {
      error = "Invalid render-target dimensions or MSAA sample count";
      return {};
    }
    int samples = std::max(info.msaaSamples, 1);
    const auto supported = [&](int count) {
      const auto flags = renderTargetFlags(count);
      return bgfx::isTextureValid(0, false, 1, textureFormat(info.colorFormat),
                                  flags) &&
             (!info.depth ||
              bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D24S8,
                                   flags | BGFX_TEXTURE_RT_WRITE_ONLY));
    };
    if (samples > 1 && !supported(samples))
      samples = 1;
    if (!supported(samples)) {
      error =
          "Render-target color/depth formats are unsupported by this device";
      return {};
    }
    TextureCreateInfo colorInfo{.width = info.width,
                                .height = info.height,
                                .format = info.colorFormat,
                                .data = {},
                                .renderTarget = true,
                                .generateMipmaps = false,
                                .filter = info.filter,
                                .debugName = info.debugName + ".color",
                                .msaaSamples = samples};
    const TextureHandle color = createTexture(colorInfo, error);
    if (!color)
      return {};
    const auto *colorRecord = textures_.find(color);
    std::array<bgfx::Attachment, 2> attachments;
    attachments[0].init(bgfx::TextureHandle{colorRecord->index},
                        bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    std::uint8_t count = 1;
    TextureHandle depthHandle;
    if (info.depth) {
      const bgfx::TextureHandle depth = bgfx::createTexture2D(
          info.width, info.height, false, 1, bgfx::TextureFormat::D24S8,
          renderTargetFlags(samples) |
              (samples > 1 ? BGFX_TEXTURE_RT_WRITE_ONLY : 0));
      if (!bgfx::isValid(depth)) {
        destroy(color);
        error = "bgfx could not create render-target depth texture.";
        return {};
      }
      depthHandle = textures_.insert({depth.idx, info.width, info.height, 0});
      attachments[1].init(depth, bgfx::Access::Write, 0, 1, 0,
                          BGFX_RESOLVE_NONE);
      count = 2;
    }
    const bgfx::FrameBufferHandle frameBuffer =
        bgfx::createFrameBuffer(count, attachments.data(), false);
    if (!bgfx::isValid(frameBuffer)) {
      if (depthHandle)
        destroy(depthHandle);
      destroy(color);
      error = "bgfx could not create framebuffer " + info.debugName + ".";
      return {};
    }
    return {.frameBuffer = frameBuffers_.insert(frameBuffer.idx),
            .color = color,
            .depth = depthHandle,
            .msaaSamples = samples};
  }

  FrameBufferHandle
  createFrameBuffer(std::span<const TextureHandle> attachments,
                    const std::string &debugName, std::string &error) override {
    const auto maximum = limits().maxFrameBufferAttachments;
    if (attachments.empty() || attachments.size() > maximum ||
        attachments.size() > UINT8_MAX) {
      error = "Framebuffer attachments exceed device limits: " + debugName;
      return {};
    }
    std::vector<bgfx::Attachment> native;
    native.reserve(attachments.size());
    for (const auto handle : attachments) {
      const auto *texture = textures_.find(handle);
      if (texture == nullptr) {
        error = "Framebuffer references an unavailable texture: " + debugName;
        return {};
      }
      native.emplace_back();
      native.back().init(bgfx::TextureHandle{texture->index},
                         bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    }
    const auto count = static_cast<std::uint8_t>(native.size());
    if (!bgfx::isFrameBufferValid(count, native.data())) {
      error =
          "Framebuffer attachment formats or sample modes are incompatible: " +
          debugName;
      return {};
    }
    const auto frameBuffer =
        bgfx::createFrameBuffer(count, native.data(), false);
    if (!bgfx::isValid(frameBuffer)) {
      error = "Could not create framebuffer: " + debugName;
      return {};
    }
    bgfx::setName(frameBuffer, debugName.c_str());
    return frameBuffers_.insert(frameBuffer.idx);
  }

  bool destroy(const TextureHandle handle) override {
    TextureRecord value;
    if (!textures_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::TextureHandle{value.index});
    return true;
  }
  bool destroy(const SamplerHandle handle) override {
    std::uint16_t value = Invalid;
    if (!samplers_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::UniformHandle{value});
    return true;
  }
  bool destroy(const UniformHandle handle) override {
    std::uint16_t value = Invalid;
    if (!uniforms_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::UniformHandle{value});
    return true;
  }
  bool destroy(const BufferHandle handle) override {
    BufferValue value;
    if (!buffers_.remove(handle, &value))
      return false;
    if (value.kind == BufferKind::Vertex)
      bgfx::destroy(bgfx::VertexBufferHandle{value.index});
    else if (value.kind == BufferKind::DynamicVertex)
      bgfx::destroy(bgfx::DynamicVertexBufferHandle{value.index});
    else
      bgfx::destroy(bgfx::IndexBufferHandle{value.index});
    return true;
  }
  bool destroy(const ProgramHandle handle) override {
    std::uint16_t value = Invalid;
    if (!programs_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::ProgramHandle{value});
    return true;
  }
  bool destroy(const FrameBufferHandle handle) override {
    std::uint16_t value = Invalid;
    if (!frameBuffers_.remove(handle, &value))
      return false;
    bgfx::destroy(bgfx::FrameBufferHandle{value});
    return true;
  }

  void clear() override {
    frameBuffers_.clear([](std::uint16_t value) {
      bgfx::destroy(bgfx::FrameBufferHandle{value});
    });
    programs_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::ProgramHandle{value}); });
    buffers_.clear([](const BufferValue &value) {
      if (value.kind == BufferKind::Vertex)
        bgfx::destroy(bgfx::VertexBufferHandle{value.index});
      else if (value.kind == BufferKind::DynamicVertex)
        bgfx::destroy(bgfx::DynamicVertexBufferHandle{value.index});
      else
        bgfx::destroy(bgfx::IndexBufferHandle{value.index});
    });
    samplers_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::UniformHandle{value}); });
    uniforms_.clear(
        [](std::uint16_t value) { bgfx::destroy(bgfx::UniformHandle{value}); });
    textures_.clear([](TextureRecord value) {
      bgfx::destroy(bgfx::TextureHandle{value.index});
    });
  }

  bgfx::TextureHandle bgfxTexture(const TextureHandle handle) const override {
    const auto *value = textures_.find(handle);
    return value != nullptr ? bgfx::TextureHandle{value->index}
                            : bgfx::TextureHandle{Invalid};
  }

  bgfx::UniformHandle bgfxSampler(const SamplerHandle handle) const override {
    const std::uint16_t *value = samplers_.find(handle);
    return value != nullptr ? bgfx::UniformHandle{*value}
                            : bgfx::UniformHandle{Invalid};
  }

  bgfx::UniformHandle bgfxUniform(const UniformHandle handle) const override {
    const std::uint16_t *value = uniforms_.find(handle);
    return value != nullptr ? bgfx::UniformHandle{*value}
                            : bgfx::UniformHandle{Invalid};
  }

  bgfx::ProgramHandle bgfxProgram(const ProgramHandle handle) const override {
    const std::uint16_t *value = programs_.find(handle);
    return value != nullptr ? bgfx::ProgramHandle{*value}
                            : bgfx::ProgramHandle{Invalid};
  }

  BgfxBufferReference bgfxBuffer(const BufferHandle handle) const override {
    const BufferValue *value = buffers_.find(handle);
    return value != nullptr
               ? BgfxBufferReference{.index = value->index, .kind = value->kind}
               : BgfxBufferReference{};
  }

  bgfx::FrameBufferHandle
  bgfxFrameBuffer(const FrameBufferHandle handle) const override {
    const std::uint16_t *value = frameBuffers_.find(handle);
    return value != nullptr ? bgfx::FrameBufferHandle{*value}
                            : bgfx::FrameBufferHandle{Invalid};
  }

private:
  ResourceHandlePool<TextureTag, TextureRecord> textures_;
  ResourceHandlePool<SamplerTag, std::uint16_t> samplers_;
  ResourceHandlePool<UniformTag, std::uint16_t> uniforms_;
  ResourceHandlePool<BufferTag, BufferValue> buffers_;
  ResourceHandlePool<ProgramTag, std::uint16_t> programs_;
  ResourceHandlePool<FrameBufferTag, std::uint16_t> frameBuffers_;
};

} // namespace

std::unique_ptr<GpuResources> createBgfxGpuResources() {
  return std::make_unique<BgfxGpuResources>();
}

} // namespace demi::runtime::render
