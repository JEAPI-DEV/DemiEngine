#pragma once

#include <cstdint>
#include <string>

namespace demi::editor {

class EditorAboutPanel {
public:
  void open() { show_ = true; }
  void draw(std::uint16_t logoTextureIndex, std::string &notice);

private:
  bool show_ = false;
};

} // namespace demi::editor
