cmake_minimum_required(VERSION 3.22)
project(DemiEcsEvaluation LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(EnTT
  URL https://codeload.github.com/skypjack/entt/tar.gz/refs/tags/v3.15.0
  URL_HASH SHA256=01466fcbf77618a79b62891510c0bbf25ac2804af5751c84982b413852234d66)
FetchContent_MakeAvailable(EnTT)
find_package(nlohmann_json 3.11 REQUIRED)
add_executable(demi-ecs-evaluation main.cpp)
target_compile_features(demi-ecs-evaluation PRIVATE cxx_std_20)
target_include_directories(demi-ecs-evaluation PRIVATE ../../../src)
target_link_libraries(demi-ecs-evaluation PRIVATE EnTT::EnTT nlohmann_json::nlohmann_json)
enable_testing()
add_test(NAME ecs-contract COMMAND demi-ecs-evaluation --verify)
