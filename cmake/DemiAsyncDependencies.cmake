# Cross-platform readiness-based TCP and the built-in database driver.
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "Build libuv tests" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "Build libuv benchmarks" FORCE)
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "Build shared libuv" FORCE)
FetchContent_Declare(demi_libuv
  URL https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz
  URL_HASH SHA256=66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7)
FetchContent_MakeAvailable(demi_libuv)

FetchContent_Declare(demi_sqlite_source
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d)
FetchContent_GetProperties(demi_sqlite_source)
if(NOT demi_sqlite_source_POPULATED)
  FetchContent_Populate(demi_sqlite_source)
endif()
add_library(demi-sqlite STATIC ${demi_sqlite_source_SOURCE_DIR}/sqlite3.c)
target_include_directories(demi-sqlite PUBLIC ${demi_sqlite_source_SOURCE_DIR})
target_compile_definitions(demi-sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1)
target_link_libraries(demi-sqlite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(demi-sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
