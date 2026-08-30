#pragma once

namespace demi::editor {

class EditorPlaySession;

class EditorDebugPanel {
public:
  void draw(EditorPlaySession &playSession);

private:
  enum class Section { Input, Renderer, Physics, Navigation, Assets, Network };
  Section section_ = Section::Input;
};

} // namespace demi::editor
