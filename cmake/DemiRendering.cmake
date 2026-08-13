# Built-in shader generation and backend-neutral bgfx renderer libraries.
set(DEMI_BGFX_BUILTIN_SHADER_DIR
  "${CMAKE_BINARY_DIR}/generated/demi/runtime/render/shaders")
set(DEMI_BGFX_SHADER_SOURCE_DIR
  "${CMAKE_SOURCE_DIR}/src/demi/runtime/render/bgfx3d/shaders")
set(DEMI_BGFX_SHADER_INCLUDE_DIR "${bgfx_SOURCE_DIR}/bgfx/src")
if(DEMI_HOST_SHADERC)
  if(NOT EXISTS "${DEMI_HOST_SHADERC}")
    message(FATAL_ERROR
      "DEMI_HOST_SHADERC does not name an existing executable: ${DEMI_HOST_SHADERC}")
  endif()
  set(DEMI_SHADERC_EXECUTABLE "${DEMI_HOST_SHADERC}")
  set(DEMI_SHADERC_DEPENDENCY "${DEMI_HOST_SHADERC}")
elseif(ANDROID)
  if(NOT DEMI_HOST_SHADERC AND DEFINED ENV{DEMI_HOST_SHADERC})
    set(DEMI_HOST_SHADERC "$ENV{DEMI_HOST_SHADERC}")
  endif()
  if(NOT DEMI_HOST_SHADERC)
    set(DEMI_HOST_SHADERC
      "${CMAKE_SOURCE_DIR}/build/linux-debug/_deps/bgfx-build/cmake/bgfx/shaderc")
  endif()
  if(NOT EXISTS "${DEMI_HOST_SHADERC}")
    message(FATAL_ERROR
      "Android shader cooking needs a host shaderc executable. Build the "
      "linux-debug shaderc target or configure -DDEMI_HOST_SHADERC=/path/to/shaderc.")
  endif()
  set(DEMI_SHADERC_EXECUTABLE "${DEMI_HOST_SHADERC}")
  set(DEMI_SHADERC_DEPENDENCY "${DEMI_HOST_SHADERC}")
else()
  set(DEMI_SHADERC_EXECUTABLE "$<TARGET_FILE:shaderc>")
  set(DEMI_SHADERC_DEPENDENCY shaderc)
endif()
set(DEMI_BGFX_BUILTIN_SHADER_HEADERS
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_glsl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_glsl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_essl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_essl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_spv.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_spv.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_glsl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_essl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_spv.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_glsl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_glsl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_essl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_essl.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_spv.h"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_spv.h")
add_custom_command(
  OUTPUT ${DEMI_BGFX_BUILTIN_SHADER_HEADERS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${DEMI_BGFX_BUILTIN_SHADER_DIR}"
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_glsl.h"
    --type vertex --platform linux -p 140
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_glsl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_glsl.h"
    --type fragment --platform linux -p 140
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_lit_glsl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_essl.h"
    --type vertex --platform android -p 100_es
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_essl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_essl.h"
    --type fragment --platform android -p 100_es
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_lit_essl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_spv.h"
    --type vertex --platform linux -p spirv
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_spv
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_lit.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_lit_spv.h"
    --type fragment --platform linux -p spirv
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_lit_spv
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit_instanced.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_glsl.h"
    --type vertex --platform linux -p 140
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_instanced_glsl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit_instanced.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_essl.h"
    --type vertex --platform android -p 100_es
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_instanced_essl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit_instanced.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_lit_instanced_spv.h"
    --type vertex --platform linux -p spirv
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_lit_instanced_spv
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_glsl.h"
    --type vertex --platform linux -p 140
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_post_process_glsl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_glsl.h"
    --type fragment --platform linux -p 140
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_post_process_glsl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_essl.h"
    --type vertex --platform android -p 100_es
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_post_process_essl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_essl.h"
    --type fragment --platform android -p 100_es
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_post_process_essl
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/vs_demi_post_process_spv.h"
    --type vertex --platform linux -p spirv
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c vs_demi_post_process_spv
  COMMAND "${DEMI_SHADERC_EXECUTABLE}" -f "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_post_process.sc"
    -o "${DEMI_BGFX_BUILTIN_SHADER_DIR}/fs_demi_post_process_spv.h"
    --type fragment --platform linux -p spirv
    --varyingdef "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
    -i "${DEMI_BGFX_SHADER_INCLUDE_DIR}" --bin2c fs_demi_post_process_spv
  DEPENDS ${DEMI_SHADERC_DEPENDENCY}
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit.sc"
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_lit.sc"
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_lit_instanced.sc"
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/vs_demi_post_process.sc"
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/fs_demi_post_process.sc"
    "${DEMI_BGFX_SHADER_SOURCE_DIR}/varying.def.sc"
  VERBATIM)

add_library(demi-graphics-bgfx STATIC
  ${DEMI_BGFX_BUILTIN_SHADER_HEADERS}
  "${DEMI_RENDER_GENERATED_INCLUDE_DIR}/demi/runtime/render/DefaultPixelFont.h"
  src/demi/runtime/render/backend/GraphicsDevice.cpp
  src/demi/runtime/render/backend/BgfxGraphicsDevice.cpp
  src/demi/runtime/render/backend/BgfxGpuResources.cpp
  src/demi/runtime/render/backend/BgfxRenderCommands.cpp
  src/demi/runtime/render/backend/BgfxVertexLayout.cpp
  src/demi/runtime/render/backend/CookedShaderLibrary.cpp
  src/demi/runtime/render/backend/Canvas2D.cpp
  src/demi/runtime/render/backend/FontAtlas2D.cpp
  src/demi/runtime/render/backend/GifDecoder2D.cpp
  src/demi/runtime/render/backend/ImageDecoder2D.cpp
  src/demi/runtime/render/backend/QuadBatch.cpp
  src/demi/runtime/render/backend/TextureLibrary2D.cpp
)
add_custom_command(
  OUTPUT
    "${DEMI_RENDER_GENERATED_INCLUDE_DIR}/demi/runtime/render/DefaultPixelFont.h"
  COMMAND "${CMAKE_COMMAND}"
    -DINPUT=${CMAKE_SOURCE_DIR}/fonts/Pixelify_Sans/static/PixelifySans-Regular.ttf
    -DOUTPUT=${DEMI_RENDER_GENERATED_INCLUDE_DIR}/demi/runtime/render/DefaultPixelFont.h
    -DSYMBOL=DefaultPixelFontData
    -P "${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/fonts/Pixelify_Sans/static/PixelifySans-Regular.ttf"
    "${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake"
  VERBATIM)
target_include_directories(demi-graphics-bgfx PUBLIC src)
target_include_directories(demi-graphics-bgfx PRIVATE
  "${DEMI_RENDER_GENERATED_INCLUDE_DIR}"
  "${DEMI_BGFX_BUILTIN_SHADER_DIR}"
  "${bgfx_SOURCE_DIR}/bgfx/examples/common/imgui"
  "${bgfx_SOURCE_DIR}/bgfx/examples/17-drawstress"
  "${bgfx_SOURCE_DIR}/bgfx/3rdparty/stb"
  "${bgfx_SOURCE_DIR}/bimg/3rdparty/stb"
  "${bgfx_SOURCE_DIR}/bimg/3rdparty/lodepng"
  "${bgfx_SOURCE_DIR}/bx/include")
target_compile_features(demi-graphics-bgfx PUBLIC cxx_std_20)
target_link_libraries(demi-graphics-bgfx
  PUBLIC demi-core
  PRIVATE bgfx
  bimg_decode
  nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-graphics-bgfx PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(demi-render2d-bgfx STATIC
  src/demi/runtime/render/BgfxRenderer2D.cpp
  src/demi/runtime/render/MaterialLibrary.cpp
  src/demi/runtime/render/ParticleSystem2D.cpp
  src/demi/runtime/render/backend/SvgDecoder2D.cpp
  src/demi/runtime/render/bgfx2d/ColliderCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/DebugCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/IsoCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/IsoSpriteVisual2D.cpp
  src/demi/runtime/render/bgfx2d/ParticleCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/SpriteCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/TilemapCanvasRenderer.cpp
  src/demi/runtime/render/bgfx2d/UiCanvasRenderer.cpp)
target_include_directories(demi-render2d-bgfx PUBLIC src)
target_compile_features(demi-render2d-bgfx PUBLIC cxx_std_20)
target_link_libraries(demi-render2d-bgfx PUBLIC demi-core demi-graphics-bgfx)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-render2d-bgfx PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(demi-render3d-bgfx STATIC
  src/demi/runtime/render/BgfxRenderer3D.cpp
  src/demi/runtime/render/bgfx3d/DebugGeometry3D.cpp
  src/demi/runtime/render/bgfx3d/GpuMesh3D.cpp
  src/demi/runtime/render/bgfx3d/MeshTransform3D.cpp
  src/demi/runtime/render/bgfx3d/PrimitiveCanvas3D.cpp
  src/demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.cpp
  src/demi/runtime/render/bgfx3d/PostProcessRenderer3D.cpp
  src/demi/runtime/render/bgfx3d/ParticleBillboardRenderer3D.cpp
  src/demi/runtime/render/bgfx3d/SceneLighting3D.cpp
  src/demi/runtime/render/bgfx3d/SceneVisibility3D.cpp
  src/demi/runtime/render/bgfx3d/WorldTextProjection3D.cpp
)
target_include_directories(demi-render3d-bgfx PUBLIC src)
target_compile_features(demi-render3d-bgfx PUBLIC cxx_std_20)
target_link_libraries(demi-render3d-bgfx
  PUBLIC demi-core demi-render2d-bgfx demi-graphics-bgfx)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-render3d-bgfx PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT ANDROID)
  find_package(PkgConfig QUIET)

  if(PkgConfig_FOUND)
    pkg_check_modules(LUA54 QUIET IMPORTED_TARGET lua5.4)
    pkg_check_modules(RSVG QUIET IMPORTED_TARGET librsvg-2.0)
    if(DEMI_ENABLE_MEDIA)
      pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    endif()
  endif()
endif()

if(DEMI_ENABLE_MEDIA AND NOT TARGET PkgConfig::FFMPEG)
  message(FATAL_ERROR "DEMI_ENABLE_MEDIA requires FFmpeg development packages: libavformat, libavcodec, libavutil, and libswscale.")
endif()

if(TARGET PkgConfig::RSVG)
  target_link_libraries(demi-render2d-bgfx PRIVATE PkgConfig::RSVG)
  target_compile_definitions(demi-render2d-bgfx PRIVATE DEMI_HAS_RSVG=1)
else()
  target_compile_definitions(demi-render2d-bgfx PRIVATE DEMI_HAS_RSVG=0)
endif()
