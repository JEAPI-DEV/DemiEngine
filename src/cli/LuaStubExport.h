#pragma once
#include <filesystem>
#include <string>
namespace demi::cli {
bool exportLuaStubs(const std::filesystem::path &source,
                    const std::filesystem::path &destination,
                    std::string &error);
}
