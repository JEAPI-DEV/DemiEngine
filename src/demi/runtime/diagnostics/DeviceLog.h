#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace demi::runtime {

namespace detail {

inline constexpr const char *DeviceLogTag = "DemiEngine";

} // namespace detail

// Device-visible engine diagnostics. On Android these are routed to logcat
// under the DemiEngine tag so device qualification captures engine lifecycle
// events; on desktop targets they mirror stderr alongside the CLI reports.
inline void deviceLog(const std::string &message) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, detail::DeviceLogTag, "%s",
                      message.c_str());
#else
  std::cerr << "[demi] " << message << '\n';
#endif
}

inline void deviceLogError(const std::string &message) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_ERROR, detail::DeviceLogTag, "%s",
                      message.c_str());
#else
  std::cerr << "[demi] " << message << '\n';
#endif
}

// Formats a native pointer for diagnostics, or "null" when absent.
inline std::string devicePointerText(const void *value) {
  if (value == nullptr)
    return "null";
  std::ostringstream stream;
  stream << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value);
  return stream.str();
}

// Builds "[channel] message" diagnostics lines with a stable prefix.
inline std::string deviceLogMessage(std::string_view channel,
                                    const std::string &message) {
  return '[' + std::string(channel) + "] " + message;
}

} // namespace demi::runtime