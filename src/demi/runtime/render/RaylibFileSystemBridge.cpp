#include "demi/runtime/render/RaylibFileSystemBridge.h"

#include <raylib.h>

#include <fstream>
#include <limits>

namespace demi::runtime {
namespace {

unsigned char *loadFileDataFromFileSystem(const char *fileName, int *dataSize) {
  if (dataSize == nullptr)
    return nullptr;
  *dataSize = 0;
  if (fileName == nullptr)
    return nullptr;

  std::ifstream input(fileName, std::ios::binary | std::ios::ate);
  if (!input)
    return nullptr;
  const std::streamoff length = input.tellg();
  if (length <= 0 || length > std::numeric_limits<int>::max())
    return nullptr;

  input.seekg(0, std::ios::beg);
  auto *bytes = static_cast<unsigned char *>(
      MemAlloc(static_cast<unsigned int>(length)));
  if (bytes == nullptr)
    return nullptr;
  if (!input.read(reinterpret_cast<char *>(bytes), length)) {
    MemFree(bytes);
    return nullptr;
  }

  *dataSize = static_cast<int>(length);
  return bytes;
}

} // namespace

void installRaylibFileSystemBridge() {
  SetLoadFileDataCallback(loadFileDataFromFileSystem);
}

} // namespace demi::runtime
