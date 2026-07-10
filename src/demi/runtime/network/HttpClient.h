#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace demi::runtime {

struct HttpResponse {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
};

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::uint16_t port = 80;
  std::string path = "/";
};

[[nodiscard]] std::string urlEncode(const std::string& value);
[[nodiscard]] std::string formEncode(const std::map<std::string, std::string>& fields);
[[nodiscard]] bool parseHttpUrl(const std::string& url, ParsedUrl& parsed, std::string& error);
[[nodiscard]] HttpResponse httpGet(const std::string& url, int timeoutMs = 5000);
[[nodiscard]] HttpResponse httpPostForm(const std::string& url, const std::map<std::string, std::string>& fields, int timeoutMs = 5000);

} // namespace demi::runtime
