#include "editor/EditorDialogLayout.h"

#include <cassert>

namespace {

using demi::editor::editorDialogLayout;
using demi::editor::EditorDialogLayoutSpec;
using demi::editor::EditorDialogSize;

constexpr EditorDialogLayoutSpec DialogSpec{
    .preferredEm = {45.0F, 22.0F},
    .minimumEm = {30.0F, 16.0F},
};

void normalWorkAreaUsesFontScaledMetrics() {
  const auto layout = editorDialogLayout({1600.0F, 900.0F}, 16.0F, DialogSpec);
  assert(layout.initial.width == 720.0F);
  assert(layout.initial.height == 352.0F);
  assert(layout.minimum.width == 480.0F);
  assert(layout.minimum.height == 256.0F);
  assert(layout.maximum.width == 1568.0F);
  assert(layout.maximum.height == 868.0F);
}

void smallWorkAreaCapsEverySizeToTheViewport() {
  const auto layout = editorDialogLayout({320.0F, 240.0F}, 16.0F, DialogSpec);
  assert(layout.initial.width == 288.0F);
  assert(layout.initial.height == 208.0F);
  assert(layout.minimum.width == layout.maximum.width);
  assert(layout.minimum.height == layout.maximum.height);
}

void highDpiUsesTheLargerFontAsItsScale() {
  const auto layout = editorDialogLayout({2560.0F, 1440.0F}, 30.0F, DialogSpec);
  assert(layout.initial.width == 1350.0F);
  assert(layout.initial.height == 660.0F);
  assert(layout.minimum.width == 900.0F);
  assert(layout.minimum.height == 480.0F);
}

void negativeDimensionsStillProduceValidConstraints() {
  constexpr EditorDialogLayoutSpec NegativeSpec{
      .preferredEm = {-45.0F, -22.0F},
      .minimumEm = {-30.0F, -16.0F},
      .viewportPaddingEm = -1.0F,
  };
  const auto layout = editorDialogLayout(EditorDialogSize{-100.0F, -50.0F},
                                         -2.0F, NegativeSpec);
  assert(layout.initial.width == 1.0F);
  assert(layout.initial.height == 1.0F);
  assert(layout.minimum.width == 1.0F);
  assert(layout.minimum.height == 1.0F);
  assert(layout.maximum.width == 1.0F);
  assert(layout.maximum.height == 1.0F);
}

} // namespace

int main() {
  normalWorkAreaUsesFontScaledMetrics();
  smallWorkAreaCapsEverySizeToTheViewport();
  highDpiUsesTheLargerFontAsItsScale();
  negativeDimensionsStillProduceValidConstraints();
}
