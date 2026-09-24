#pragma once
#include <string>
#include <vector>
namespace demi::runtime::platform {
// Launch argv directly. No shell expansion or command-string interpretation.
bool launchExternalProcess(const std::vector<std::string> &arguments,
                           std::string &error);
} // namespace demi::runtime::platform
