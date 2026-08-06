cmake_minimum_required(VERSION 3.22)

project(DemiEngine
  VERSION 0.1.0
  DESCRIPTION "AI-friendly C++/Lua game engine scaffold for Linux 2D and isometric 2.5D games"
  LANGUAGES C CXX
)

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
