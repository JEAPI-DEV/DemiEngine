#include "demi/core/Version.h"

#include <iostream>

int main(int argc, char** argv) {
  std::cout << demi::EngineName << " editor " << demi::EngineVersion << '\n';
  std::cout << "Editor subsystem placeholder for future raylib/Dear ImGui integration.\n";
  if (argc > 1) {
    std::cout << "Arguments:";
    for (int i = 1; i < argc; ++i) {
      std::cout << ' ' << argv[i];
    }
    std::cout << '\n';
  }
  return 0;
}
