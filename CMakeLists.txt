cmake_minimum_required(VERSION 3.25)

project(DemiEngine
  VERSION 0.1.0
  DESCRIPTION "AI-friendly C++/Lua game engine scaffold for Linux 2D and isometric 2.5D games"
  LANGUAGES CXX
)

include(FetchContent)

FetchContent_Declare(sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444
)
FetchContent_MakeAvailable(sol2)

option(DEMI_ENABLE_NETWORK "Enable optional ENet networking module" OFF)
option(DEMI_ENABLE_MEDIA "Enable FFmpeg-backed media module" OFF)

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

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
  endif()
endif()

if(PkgConfig_FOUND)
  pkg_check_modules(LUA54 QUIET IMPORTED_TARGET lua5.4)
  pkg_check_modules(MPG123 QUIET IMPORTED_TARGET libmpg123)
endif()

add_library(demi-runtime-lib STATIC
  src/demi/runtime/AudioSystem.cpp
  src/demi/runtime/LuaScriptHostBindings.cpp
  src/demi/runtime/LuaScriptHost.cpp
  src/demi/runtime/LuaScriptHostPersistence.cpp
  src/demi/runtime/LuaScriptHostServices.cpp
  src/demi/runtime/Physics2D.cpp
  src/demi/runtime/Renderer2D.cpp
  src/demi/runtime/RuntimeApp.cpp
  src/demi/runtime/SceneLoader.cpp
)

target_link_libraries(demi-runtime-lib PUBLIC demi-core)
target_include_directories(demi-runtime-lib PUBLIC src)
target_compile_features(demi-runtime-lib PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-runtime-lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TARGET SDL3::SDL3)
  target_link_libraries(demi-runtime-lib PRIVATE SDL3::SDL3)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_SDL3=1)
elseif(TARGET PkgConfig::SDL3)
  target_link_libraries(demi-runtime-lib PRIVATE PkgConfig::SDL3)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_SDL3=1)
else()
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_SDL3=0)
  message(WARNING "SDL3 was not found. demi run will build but cannot open a game window.")
endif()

if(TARGET PkgConfig::LUA54)
  target_link_libraries(demi-runtime-lib PRIVATE PkgConfig::LUA54)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_LUA54=1)
else()
  message(FATAL_ERROR "Lua 5.4 was not found. Install lua5.4 development files to build DemiEngine scripting.")
endif()

target_link_libraries(demi-runtime-lib PRIVATE sol2::sol2)
target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_SOL2=1)

if(TARGET PkgConfig::MPG123)
  target_link_libraries(demi-runtime-lib PRIVATE PkgConfig::MPG123)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_MPG123=1)
else()
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_MPG123=0)
  message(WARNING "libmpg123 was not found. MP3 audio assets will not play.")
endif()

add_executable(demi src/cli/main.cpp)
target_link_libraries(demi PRIVATE demi-core demi-runtime-lib)
target_compile_definitions(demi PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(demi-runtime src/runtime/main.cpp)
target_link_libraries(demi-runtime PRIVATE demi-core demi-runtime-lib)

add_executable(demi-editor src/editor/main.cpp)
target_link_libraries(demi-editor PRIVATE demi-core)

add_executable(demi-smoke-tests tests/smoke_tests.cpp)
target_link_libraries(demi-smoke-tests PRIVATE demi-core)

add_executable(demi-physics2d-tests tests/physics2d_tests.cpp)
target_link_libraries(demi-physics2d-tests PRIVATE demi-runtime-lib)

add_executable(demi-lua-scripting-tests tests/lua_scripting_tests.cpp)
target_link_libraries(demi-lua-scripting-tests PRIVATE demi-runtime-lib)

enable_testing()
add_test(NAME demi-smoke-tests COMMAND demi-smoke-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-physics2d-tests COMMAND demi-physics2d-tests)
add_test(NAME demi-lua-scripting-tests COMMAND demi-lua-scripting-tests)
add_test(NAME demi-runtime-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-frame PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
add_test(NAME demi-script-check-minimal-player
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d/scripts/player.lua
)
add_test(NAME demi-lua-stubs-generate
  COMMAND demi lua-stubs generate ${CMAKE_BINARY_DIR}/generated/demi.lua
)
