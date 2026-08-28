cmake_minimum_required(VERSION 3.22)

project(DemiEngine
  VERSION 0.1.0
  DESCRIPTION "AI-friendly C++/Lua game engine scaffold for Linux 2D and isometric 2.5D games"
  LANGUAGES C CXX
)

set(DEMI_BUILD_ITERATION "" CACHE STRING
  "Build iteration shown in product identity; defaults to the Git commit count")
if(DEMI_BUILD_ITERATION STREQUAL "")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE DEMI_BUILD_ITERATION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
  if(NOT DEMI_BUILD_ITERATION MATCHES "^[0-9]+$")
    set(DEMI_BUILD_ITERATION "0")
  endif()
endif()
if(NOT DEMI_BUILD_ITERATION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "DEMI_BUILD_ITERATION must be a non-negative integer")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/demi/core")
configure_file("${CMAKE_SOURCE_DIR}/cmake/BuildVersion.h.in"
  "${CMAKE_BINARY_DIR}/generated/demi/core/BuildVersion.h" @ONLY)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DemiDependencies)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(DemiComponentModel)
include(DemiCore)
include(DemiRendering)
include(DemiRuntime)
include(DemiApplications)

if(NOT ANDROID)
  enable_testing()
  include(DemiTestTargets)
  include(DemiTestRegistrations)
  include(DemiExampleTests)
endif()
