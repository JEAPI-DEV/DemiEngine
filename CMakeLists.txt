cmake_minimum_required(VERSION 3.22)

project(DemiEngine
  VERSION 0.1.0
  DESCRIPTION "AI-friendly C++/Lua game engine scaffold for Linux 2D and isometric 2.5D games"
  LANGUAGES C CXX
)

include(FetchContent)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

if(ANDROID)
  set(DEMI_ENABLE_MEDIA OFF CACHE BOOL "Disable FFmpeg media on Android v1" FORCE)
  if(NOT DEFINED ANDROID_NDK AND DEFINED CMAKE_ANDROID_NDK)
    set(ANDROID_NDK "${CMAKE_ANDROID_NDK}")
  endif()
  set(PLATFORM Android CACHE STRING "raylib target platform" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444
)
FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
  FetchContent_Populate(sol2)
endif()
add_library(sol2 INTERFACE)
add_library(sol2::sol2 ALIAS sol2)
target_include_directories(sol2 INTERFACE ${sol2_SOURCE_DIR}/include)

set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_USER_SETTINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v2.4.1
)
FetchContent_MakeAvailable(box2d)

FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.22
)
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
  GIT_TAG v3.6.2
)
FetchContent_MakeAvailable(mbedtls)

FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
  FetchContent_Populate(lua)
endif()

function(add_demi_lua_static target)
  add_library(${target} STATIC
    ${lua_SOURCE_DIR}/lapi.c
    ${lua_SOURCE_DIR}/lauxlib.c
    ${lua_SOURCE_DIR}/lbaselib.c
    ${lua_SOURCE_DIR}/lcode.c
    ${lua_SOURCE_DIR}/lcorolib.c
    ${lua_SOURCE_DIR}/lctype.c
    ${lua_SOURCE_DIR}/ldblib.c
    ${lua_SOURCE_DIR}/ldebug.c
    ${lua_SOURCE_DIR}/ldo.c
    ${lua_SOURCE_DIR}/ldump.c
    ${lua_SOURCE_DIR}/lfunc.c
    ${lua_SOURCE_DIR}/lgc.c
    ${lua_SOURCE_DIR}/linit.c
    ${lua_SOURCE_DIR}/liolib.c
    ${lua_SOURCE_DIR}/llex.c
    ${lua_SOURCE_DIR}/lmathlib.c
    ${lua_SOURCE_DIR}/lmem.c
    ${lua_SOURCE_DIR}/loadlib.c
    ${lua_SOURCE_DIR}/lobject.c
    ${lua_SOURCE_DIR}/lopcodes.c
    ${lua_SOURCE_DIR}/loslib.c
    ${lua_SOURCE_DIR}/lparser.c
    ${lua_SOURCE_DIR}/lstate.c
    ${lua_SOURCE_DIR}/lstring.c
    ${lua_SOURCE_DIR}/lstrlib.c
    ${lua_SOURCE_DIR}/ltable.c
    ${lua_SOURCE_DIR}/ltablib.c
    ${lua_SOURCE_DIR}/ltm.c
    ${lua_SOURCE_DIR}/lundump.c
    ${lua_SOURCE_DIR}/lutf8lib.c
    ${lua_SOURCE_DIR}/lvm.c
    ${lua_SOURCE_DIR}/lzio.c
  )
  target_include_directories(${target} PUBLIC ${lua_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE LUA_USE_POSIX)
endfunction()

if(ANDROID)
  add_demi_lua_static(lua54)
else()
  add_demi_lua_static(demi-server-lua54)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RPRAND ON CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

option(DEMI_ENABLE_NETWORK "Enable optional ENet networking module" OFF)
option(DEMI_ENABLE_MEDIA "Enable FFmpeg-backed media module" ON)

if(DEMI_ENABLE_NETWORK)
  FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
  )
  FetchContent_MakeAvailable(enet)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(demi-core STATIC
  src/demi/assets/AssetRegistry.cpp
  src/demi/diagnostics/Diagnostic.cpp
  src/demi/filesystem/ProjectPaths.cpp
  src/demi/schema/Validation.cpp
)

target_include_directories(demi-core PUBLIC src)

target_compile_features(demi-core PUBLIC cxx_std_20)

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

set(DEMI_RUNTIME_COMMON_SOURCES
  src/demi/runtime/app/RuntimeApp.cpp
  src/demi/runtime/audio/AudioClipPreprocess.cpp
  src/demi/runtime/audio/AudioSystem.cpp
  src/demi/runtime/audio/MiniaudioAudioBackend.cpp
  src/demi/runtime/media/MediaSystem.cpp
  src/demi/runtime/network/DtlsTransport.cpp
  src/demi/runtime/network/HttpClient.cpp
  src/demi/runtime/network/NetworkSystem.cpp
  src/demi/runtime/network/TlsMessaging.cpp
  src/demi/runtime/physics/Physics2D.cpp
  src/demi/runtime/physics/Physics3D.cpp
  src/demi/runtime/profiling/RuntimeProfiler.cpp
  src/demi/runtime/scene/HudParser.cpp
  src/demi/runtime/scene/hud/HudElementRegistry.cpp
  src/demi/runtime/scene/hud/elements/HudButtonElement.cpp
  src/demi/runtime/scene/hud/elements/HudCircleElement.cpp
  src/demi/runtime/scene/hud/elements/HudImageElement.cpp
  src/demi/runtime/scene/hud/elements/HudPanelElement.cpp
  src/demi/runtime/scene/hud/elements/HudRectElement.cpp
  src/demi/runtime/scene/hud/elements/HudTextElement.cpp
  src/demi/runtime/scene/ComponentRegistry.cpp
  src/demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/Camera2DComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/IsoGridComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/IsoTransformComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/SpriteComponent.cpp
  src/demi/runtime/scene/components/2dcomponents/Transform2DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/Camera3DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/MeshRendererComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.cpp
  src/demi/runtime/scene/components/3dcomponents/Transform3DComponent.cpp
  src/demi/runtime/scene/components/gameplay/BuildableComponent.cpp
  src/demi/runtime/scene/components/gameplay/HitboxControllerComponent.cpp
  src/demi/runtime/scene/components/gameplay/LuaScriptComponent.cpp
  src/demi/runtime/scene/components/media/AudioListenerComponent.cpp
  src/demi/runtime/scene/components/media/AudioSourceComponent.cpp
  src/demi/runtime/scene/components/media/VideoPlayerComponent.cpp
  src/demi/runtime/scene/ProjectParser.cpp
  src/demi/runtime/scene/SceneEntityParser.cpp
  src/demi/runtime/scene/SceneJson.cpp
  src/demi/runtime/scene/SceneLoader.cpp
  src/demi/runtime/scripting/LuaScriptHostBindings.cpp
  src/demi/runtime/scripting/LuaScriptHost.cpp
  src/demi/runtime/scripting/LuaBindingCleanup.cpp
  src/demi/runtime/scripting/annotations/HandleActionAnnotation.cpp
  src/demi/runtime/scripting/annotations/LuaAnnotationScanner.cpp
  src/demi/runtime/scripting/annotations/LuaModulePath.cpp
  src/demi/runtime/scripting/annotations/OnEventAnnotation.cpp
  src/demi/runtime/scripting/LuaScriptDiagnostics.cpp
  src/demi/runtime/scripting/LuaScriptHostEntityServices.cpp
  src/demi/runtime/scripting/LuaScriptHostHudServices.cpp
  src/demi/runtime/scripting/LuaScriptHostInputDebugServices.cpp
  src/demi/runtime/scripting/LuaScriptHostLoading.cpp
  src/demi/runtime/scripting/LuaScriptHostMediaNetworkServices.cpp
  src/demi/runtime/scripting/LuaScriptProperties.cpp
  src/demi/runtime/scripting/bindings/LuaBindingHelpers.cpp
  src/demi/runtime/scripting/bindings/LuaCoreBindings.cpp
  src/demi/runtime/scripting/bindings/LuaEntityBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaRigidbody2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaTransform2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaTransform3DBindings.cpp
  src/demi/runtime/scripting/bindings/hud/LuaHudBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaAudioBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaCutsceneBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaVideoBindings.cpp
  src/demi/runtime/scripting/bindings/persistence/LuaSaveBindings.cpp
  src/demi/runtime/scripting/bindings/LuaJsonBridge.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkBindings.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkSessionBindings.cpp
  src/demi/runtime/scripting/bindings/LuaTlsBindings.cpp
  src/demi/runtime/scripting/persistence/LuaSaveCodec.cpp
  src/demi/runtime/scripting/LuaScriptHostPersistence.cpp
  src/demi/runtime/scripting/LuaScriptHostServices.cpp
)

function(configure_demi_runtime target with_renderer)
  add_library(${target} STATIC ${DEMI_RUNTIME_COMMON_SOURCES} ${ARGN})
  target_link_libraries(${target} PUBLIC demi-core)
  target_include_directories(${target} PUBLIC src)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_link_libraries(${target} PRIVATE box2d miniaudio nlohmann_json::nlohmann_json mbedtls mbedx509 mbedcrypto sol2::sol2)
  target_compile_definitions(${target} PRIVATE
    DEMI_HAS_BOX2D=1
    DEMI_HAS_MINIAUDIO=1
    DEMI_HAS_MBEDTLS=1
    DEMI_HAS_SOL2=1
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(${with_renderer})
    target_link_libraries(${target} PRIVATE raylib)
    target_compile_definitions(${target} PRIVATE DEMI_HAS_RAYLIB=1)
    if(TARGET PkgConfig::RSVG)
      target_link_libraries(${target} PRIVATE PkgConfig::RSVG)
      target_compile_definitions(${target} PRIVATE DEMI_HAS_RSVG=1)
    else()
      target_compile_definitions(${target} PRIVATE DEMI_HAS_RSVG=0)
    endif()
  else()
    target_compile_definitions(${target} PRIVATE DEMI_HAS_RAYLIB=0 DEMI_HAS_RSVG=0)
  endif()

  if(ANDROID)
    target_link_libraries(${target} PRIVATE lua54)
  elseif(NOT ${with_renderer})
    target_include_directories(${target} BEFORE PRIVATE
      ${CMAKE_SOURCE_DIR}/src/demi/runtime/scripting/lua_compat
      ${lua_SOURCE_DIR}
    )
    target_link_libraries(${target} PRIVATE demi-server-lua54 ${CMAKE_DL_LIBS})
  elseif(TARGET PkgConfig::LUA54)
    target_link_libraries(${target} PRIVATE PkgConfig::LUA54)
  else()
    message(FATAL_ERROR "Lua 5.4 was not found. Install lua5.4 development files to build DemiEngine scripting.")
  endif()

  if(DEMI_ENABLE_MEDIA)
    target_link_libraries(${target} PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(${target} PRIVATE DEMI_HAS_FFMPEG=1)
  else()
    target_compile_definitions(${target} PRIVATE DEMI_HAS_FFMPEG=0)
  endif()

  if(DEMI_ENABLE_NETWORK)
    target_link_libraries(${target} PRIVATE enet)
    target_include_directories(${target} PRIVATE ${enet_SOURCE_DIR}/include)
    target_compile_definitions(${target} PRIVATE DEMI_HAS_ENET=1)
  else()
    target_compile_definitions(${target} PRIVATE DEMI_HAS_ENET=0)
  endif()
endfunction()

configure_demi_runtime(demi-runtime-lib TRUE
  src/demi/runtime/render/Renderer2D.cpp
  src/demi/runtime/render/Renderer3D.cpp
  src/demi/runtime/render/Renderer3DAssets.cpp
  src/demi/runtime/render/Renderer3DHudRenderer.cpp
  src/demi/runtime/render/Renderer3DMeshRenderer.cpp
  src/demi/runtime/render/Renderer3DModelResources.cpp
)

if(NOT ANDROID)
  configure_demi_runtime(demi-server-runtime-lib FALSE)
endif()

if(ANDROID)
  add_library(demi_android SHARED src/android/main.cpp)
  target_link_libraries(demi_android PRIVATE demi-runtime-lib)
  target_include_directories(demi_android PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
  target_link_libraries(demi_android PRIVATE android log OpenSLES EGL GLESv2)
  target_link_options(demi_android PRIVATE "LINKER:-u,ANativeActivity_onCreate")
else()
  add_executable(demi
    src/cli/BuildCommands.cpp
    src/cli/main.cpp
  )
  target_link_libraries(demi PRIVATE demi-core demi-runtime-lib)
  target_compile_definitions(demi PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  add_executable(demi-server
    src/cli/BuildCommands.cpp
    src/cli/main.cpp
  )
  target_link_libraries(demi-server PRIVATE demi-core demi-server-runtime-lib)
  target_compile_definitions(demi-server PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  add_executable(demi-runtime src/runtime/main.cpp)
  target_link_libraries(demi-runtime PRIVATE demi-core demi-runtime-lib)

  add_executable(demi-editor src/editor/main.cpp)
  target_link_libraries(demi-editor PRIVATE demi-core)

  add_executable(demi-smoke-tests tests/smoke_tests.cpp)
  target_link_libraries(demi-smoke-tests PRIVATE demi-core)

  add_executable(demi-physics2d-tests tests/physics2d_tests.cpp)
  target_link_libraries(demi-physics2d-tests PRIVATE demi-runtime-lib)

  add_executable(demi-physics3d-tests tests/physics3d_tests.cpp)
  target_link_libraries(demi-physics3d-tests PRIVATE demi-runtime-lib)

  add_executable(demi-lua-stub-contract-tests tests/lua_stub_contract_tests.cpp)
  target_link_libraries(demi-lua-stub-contract-tests PRIVATE demi-runtime-lib)

  add_executable(demi-lua-scripting-tests tests/lua_scripting_tests.cpp)
  target_link_libraries(demi-lua-scripting-tests PRIVATE demi-runtime-lib)

  add_executable(demi-scene-loader-tests tests/scene_loader_tests.cpp)
  target_link_libraries(demi-scene-loader-tests PRIVATE demi-runtime-lib)

  add_executable(demi-snapshot-replication-tests tests/snapshot_replication_tests.cpp)
  target_link_libraries(demi-snapshot-replication-tests PRIVATE demi-runtime-lib)

  if(DEMI_ENABLE_NETWORK)
    add_executable(demi-network-tests tests/network_tests.cpp)
    target_link_libraries(demi-network-tests PRIVATE demi-runtime-lib)
    target_compile_definitions(demi-network-tests PRIVATE
      DEMI_DTLS_TEST_CERT="${mbedtls_SOURCE_DIR}/framework/data_files/server5.crt"
      DEMI_DTLS_TEST_KEY="${mbedtls_SOURCE_DIR}/framework/data_files/server5.key"
      DEMI_DTLS_TEST_CA="${mbedtls_SOURCE_DIR}/framework/data_files/test-ca2.crt"
    )
    add_executable(demi-lua-server-tests tests/lua_server_tests.cpp)
    target_link_libraries(demi-lua-server-tests PRIVATE demi-runtime-lib)
  endif()

  enable_testing()
add_test(NAME demi-smoke-tests COMMAND demi-smoke-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-physics2d-tests COMMAND demi-physics2d-tests)
add_test(NAME demi-physics3d-tests COMMAND demi-physics3d-tests)
add_test(NAME demi-lua-stub-contract-tests COMMAND demi-lua-stub-contract-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-lua-scripting-tests COMMAND demi-lua-scripting-tests)
add_test(NAME demi-scene-loader-tests COMMAND demi-scene-loader-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-snapshot-replication-tests COMMAND demi-snapshot-replication-tests)
if(DEMI_ENABLE_NETWORK)
  add_test(NAME demi-network-tests COMMAND demi-network-tests)
  add_test(
    NAME demi-lua-server-tests
    COMMAND demi-lua-server-tests
      $<TARGET_FILE:demi>
      ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android_server/demi.project.json
      ${mbedtls_SOURCE_DIR}/framework/data_files/server5.crt
      ${mbedtls_SOURCE_DIR}/framework/data_files/server5.key
      ${mbedtls_SOURCE_DIR}/framework/data_files/test-ca2.crt
      localhost
  )
endif()
add_test(NAME demi-runtime-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-networking-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-networking-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-minimal-3d-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-3d-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-minimal-voxel-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-voxel-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-animated-main-menu-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/main_menu_animated/demi.project.json --frames 3
)
set_tests_properties(demi-runtime-animated-main-menu-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-gif-main-menu-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/main_menu_gif/demi.project.json --frames 3
)
set_tests_properties(demi-runtime-gif-main-menu-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-validate-minimal-2d-networking
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json
)
add_test(NAME demi-validate-minimal-2d-android
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android/demi.project.json
)
add_test(NAME demi-validate-minimal-3d
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json
)
add_test(NAME demi-validate-minimal-voxel
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json
)
add_test(NAME demi-validate-animated-main-menu
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/main_menu_animated/demi.project.json
)
add_test(NAME demi-validate-gif-main-menu
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/main_menu_gif/demi.project.json
)
add_test(NAME demi-script-check-minimal-player
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/scripts/player.lua
)
add_test(NAME demi-script-check-minimal-voxel-fly-camera
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/fly_camera.lua
)
add_test(NAME demi-script-check-minimal-voxel-worldgen
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/worldgen.lua
)
foreach(script_name
  biomes
  config
  terrain
  mesh
  chunks
  interaction
  inventory
  performance
  state
)
  add_test(NAME demi-script-check-minimal-voxel-worldgen-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/worldgen/${script_name}.lua
  )
endforeach()
add_test(NAME demi-script-check-minimal-voxel-import-pack
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/tools/import_pack.lua
)
add_test(NAME demi-script-check-runtime-net-snapshot-replication
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/net/snapshot_replication.lua
)
set(DEMI_NETWORKING_EXAMPLE_SCRIPTS
  game
  game/collectibles
  game/hud
  game/score
  game/world
  game_state
  levels/platformer
  levels/spiral
  main_menu
  menu/actions
  menu/network
  menu/settings
  menu/view
  menu_scene
  network_replication
  player
  player_config
  player_platformer
  player_slingshot
)
foreach(script_name IN LISTS DEMI_NETWORKING_EXAMPLE_SCRIPTS)
  add_test(NAME demi-script-check-networking-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/scripts/${script_name}.lua
  )
endforeach()
set(DEMI_ANDROID_EXAMPLE_SCRIPTS
  game
  game/collectibles
  game/hud
  game/platforms
  game/score
  game/world
  game_state
  levels/platformer
  levels/spiral
  main_menu
  menu/actions
  menu/network
  menu/settings
  menu/view
  menu_scene
  network_lobby
  network_replication
  player
  player_colors
  player_config
  player_platformer
  player_slingshot
)
foreach(script_name IN LISTS DEMI_ANDROID_EXAMPLE_SCRIPTS)
  add_test(NAME demi-script-check-android-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-lua-stubs-generate
  COMMAND demi lua-stubs generate ${CMAKE_BINARY_DIR}/generated/demi.lua
)
endif()
