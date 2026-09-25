# HTTP client dependency configuration shared by runtime and host tools.
include_guard(GLOBAL)

find_package(Threads REQUIRED)

if(NOT ANDROID)
  # Demi's HTTP service uses CURLOPT_PROTOCOLS_STR (introduced in 7.85),
  # CURLOPT_CAINFO_BLOB, and curl_multi_wakeup. Keep the desktop requirement
  # compatible with maintained 7.88 and 8.x distribution packages.
  find_package(CURL 7.88 REQUIRED)
  return()
endif()

# Android has no NDK libcurl. Build a small, reproducible HTTP/HTTPS-only
# library against the mbedTLS targets already populated by DemiDependencies.
function(demi_configure_android_curl)
  # Keep generic dependency options local to curl so Demi's own CTest and
  # later FetchContent projects retain their configuration.
  set(BUILD_CURL_EXE OFF)
  set(BUILD_EXAMPLES OFF)
  set(BUILD_LIBCURL_DOCS OFF)
  set(BUILD_MISC_DOCS OFF)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS ON)
  set(BUILD_TESTING OFF)
  set(CURL_DISABLE_INSTALL ON)
  set(ENABLE_CURL_MANUAL OFF)

  set(CURL_ENABLE_SSL ON)
  set(CURL_USE_MBEDTLS ON)
  set(CURL_USE_OPENSSL OFF)
  set(CURL_USE_GNUTLS OFF)
  set(CURL_USE_RUSTLS OFF)
  set(CURL_USE_SCHANNEL OFF)
  set(CURL_USE_WOLFSSL OFF)

  set(ENABLE_ARES OFF)
  set(ENABLE_THREADED_RESOLVER ON)
  set(HTTP_ONLY ON)
  set(CURL_USE_PKGCONFIG OFF)
  set(CURL_USE_CMAKECONFIG OFF)
  set(CURL_USE_LIBPSL OFF)
  set(CURL_USE_LIBSSH OFF)
  set(CURL_USE_LIBSSH2 OFF)
  set(CURL_USE_GSASL OFF)
  set(CURL_USE_GSSAPI OFF)
  set(USE_LIBIDN2 OFF)
  set(USE_NGHTTP2 OFF)
  set(USE_NGTCP2 OFF)
  set(USE_QUICHE OFF)
  set(CURL_BROTLI OFF CACHE STRING "" FORCE)
  # The Android NDK supplies zlib. AUTO preserves compressed HTTP delivery
  # when the active toolchain exposes it without introducing a fetched library.
  set(CURL_ZLIB AUTO CACHE STRING "" FORCE)
  set(CURL_ZSTD OFF CACHE STRING "" FORCE)

  # Cross-compilation must not bake host CA paths into the Android library.
  # The native HTTP service selects Android's current system trust directories
  # at runtime; callers may also provide an explicit CA blob.
  set(CURL_CA_BUNDLE none CACHE STRING "" FORCE)
  set(CURL_CA_PATH none CACHE STRING "" FORCE)

  # curl's FindMbedTLS module normally searches installed libraries. Point it
  # at the official in-tree aliases exported by the already-populated mbedTLS
  # 3.6.2 source so the Android build never falls back to host packages.
  set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include")
  set(MBEDTLS_LIBRARY MbedTLS::mbedtls)
  set(MBEDX509_LIBRARY MbedTLS::mbedx509)
  set(MBEDCRYPTO_LIBRARY MbedTLS::mbedcrypto)
  set(MBEDTLS_USE_STATIC_LIBS ON)

  # curl 8.22 probes this mbedTLS 3.x symbol by linking a standalone
  # try_compile project. That project cannot resolve aliases to parent build
  # targets, so derive this one capability from the configured mbedTLS headers
  # with a compile-only check before curl configures.
  include(CheckCSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_INCLUDES "${mbedtls_SOURCE_DIR}/include")
  get_target_property(DEMI_MBEDCRYPTO_TARGET
    MbedTLS::mbedcrypto ALIASED_TARGET)
  if(DEMI_MBEDCRYPTO_TARGET)
    get_target_property(DEMI_MBEDTLS_COMPILE_DEFINITIONS
      ${DEMI_MBEDCRYPTO_TARGET} INTERFACE_COMPILE_DEFINITIONS)
    foreach(DEMI_MBEDTLS_DEFINITION IN LISTS DEMI_MBEDTLS_COMPILE_DEFINITIONS)
      if(DEMI_MBEDTLS_DEFINITION MATCHES
          "^MBEDTLS_(USER_)?CONFIG_FILE=")
        list(APPEND CMAKE_REQUIRED_DEFINITIONS
          "-D${DEMI_MBEDTLS_DEFINITION}")
      endif()
    endforeach()
  endif()
  set(DEMI_SAVED_TRY_COMPILE_TARGET_TYPE
    "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  unset(DEMI_MBEDTLS_HAS_DES_CRYPT_ECB CACHE)
  check_c_source_compiles([=[
    #include <mbedtls/build_info.h>
    #if !defined(MBEDTLS_DES_C)
    #error "mbedTLS DES support is disabled"
    #endif
    #include <mbedtls/des.h>
    int main(void) {
      mbedtls_des_context context;
      unsigned char input[8] = {0};
      unsigned char output[8] = {0};
      return mbedtls_des_crypt_ecb(&context, input, output);
    }
  ]=] DEMI_MBEDTLS_HAS_DES_CRYPT_ECB)
  if(DEMI_SAVED_TRY_COMPILE_TARGET_TYPE)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE
      "${DEMI_SAVED_TRY_COMPILE_TARGET_TYPE}")
  else()
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  endif()
  cmake_pop_check_state()
  set(HAVE_MBEDTLS_DES_CRYPT_ECB
    "${DEMI_MBEDTLS_HAS_DES_CRYPT_ECB}" CACHE INTERNAL
    "mbedTLS provides mbedtls_des_crypt_ecb" FORCE)

  set(DEMI_CURL_EXTRACT_OPTIONS "")
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    set(DEMI_CURL_EXTRACT_OPTIONS DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  FetchContent_Declare(demi_curl
    URL https://curl.se/download/curl-8.22.0.tar.xz
    URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
    ${DEMI_CURL_EXTRACT_OPTIONS}
  )
  FetchContent_MakeAvailable(demi_curl)
endfunction()

demi_configure_android_curl()

if(NOT TARGET CURL::libcurl)
  message(FATAL_ERROR "The configured curl dependency did not export CURL::libcurl")
endif()
