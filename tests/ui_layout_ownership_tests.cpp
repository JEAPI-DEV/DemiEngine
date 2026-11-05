#include "demi/runtime/ui/UiAccessibilityTree.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiStateController.h"
#include "demi/runtime/ui/UiTweenSystem.h"
#include "demi/runtime/ui/UiVirtualCollection.h"
#include <algorithm>
#include <cassert>

using namespace demi::runtime;
using namespace demi::runtime::ui;

Rect bounds(const UiDocument &document, const std::string &id) {
  const auto snapshot = UiAccessibilityTree::snapshot(document);
  const auto found = std::ranges::find(snapshot, id, &UiAccessibilityNode::id);
  assert(found != snapshot.end());
  return found->bounds;
}

int main() {
  UiDocument document;
  UiNode row;
  row.id = "row";
  row.type = "container";
  row.layout.size = {200, 50};
  row.layout.direction = LayoutDirection::Row;
  row.layout.gap = 2;
  document.nodes.push_back(row);
  for (const std::string id : {"a", "b"}) {
    UiNode button;
    button.id = id;
    button.parent = "row";
    button.type = "button";
    button.text = id;
    button.focusable = true;
    button.layout.size = {20, 20};
    document.nodes.push_back(button);
  }
  UiMutationQueue::initializeGenerations(document);
  UiStateController state;
  assert(bounds(document, "b").x == 22);
  const auto first = document.layoutRevision();
  for (int width = 21; width <= 50; ++width) {
    assert(state.setSize(document, "a", {float(width), 20}));
  }
  assert(document.layoutRevision() == first);
  assert(bounds(document, "b").x == 52);
  assert(document.layoutRevision() == first + 1);
  for (int repeat = 0; repeat < 20; ++repeat) {
    assert(state.setSize(document, "a", {50, 20}));
    assert(state.setVisible(document, "a", true));
    assert(state.setText(document, "a", "a"));
    UiLayoutEngine{}.ensure(document, document.canvasSize);
  }
  assert(document.layoutRevision() == first + 1);

  assert(state.setVisible(document, "a", false));
  assert(bounds(document, "b").x == 0);
  assert(state.setPosition(document, "b", {70, 0}));
  UiInteractionController{}.movePointer(document, 0, {75, 5});
  assert(state.find(document, "b")->hovered);
  assert(state.find(document, "b")->resolved.x == 70);

  assert(state.setPosition(document, "b", {80, 0}));
  const auto beforeCopy = document.layoutRevision();
  const UiDocument copy = document;
  assert(bounds(copy, "b").x == 80);
  assert(document.layoutRevision() == beforeCopy);
  assert(copy.layoutRevision() == beforeCopy + 1);
  assert(bounds(document, "b").x == 80);

  UiTweenSystem tweens;
  const auto button = *UiMutationQueue::handle(document, "b");
  assert(tweens.start(document, button, UiTweenProperty::PositionX, 100, 0));
  assert(bounds(document, "b").x == 100);
  const auto beforeOpacity = document.layoutRevision();
  assert(tweens.start(document, button, UiTweenProperty::Opacity, 0.5F, 0));
  UiLayoutEngine{}.ensure(document, document.canvasSize);
  assert(document.layoutRevision() == beforeOpacity);
  tweens.setReducedMotion(true);
  assert(tweens.start(document, button, UiTweenProperty::PositionX, 110, 1));
  assert(bounds(document, "b").x == 110);

  UiMutationQueue mutation;
  mutation.reparent(button, "");
  assert(mutation.apply(document).applied);
  assert(bounds(document, "b").x == 110);
  const auto beforeFailedMutation = document.layoutRevision();
  assert(mutation.apply(document).applied);
  assert(bounds(document, "b").x == 110);
  assert(document.layoutRevision() == beforeFailedMutation);
  mutation.reparent(button, "b");
  assert(!mutation.apply(document).applied);
  assert(bounds(document, "b").x == 110);
  assert(document.layoutRevision() == beforeFailedMutation);

  UiDocument rows;
  UiNode rowTemplate;
  rowTemplate.id = "template";
  rowTemplate.type = "label";
  rowTemplate.text = "Row";
  rowTemplate.layout.size = {100, 20};
  rows.nodes.push_back(rowTemplate);
  UiMutationQueue::initializeGenerations(rows);
  UiVirtualRecycler recycler("list");
  UiTweenSystem rowTweens;
  const auto prototype = *UiMutationQueue::handle(rows, "template");
  const std::vector<std::string> keys{"one", "two"};
  const std::vector<float> shortRows{20, 20};
  const std::vector<float> tallRows{40, 20};
  auto reconciled =
      recycler.reconcile(rows, rowTweens, prototype, keys, shortRows, 0, 20, 0);
  assert(reconciled.applied && !reconciled.rows.empty());
  const auto rowId = reconciled.rows.front().node.id;
  assert(bounds(rows, rowId).height == 20);
  const auto oldRevision = rows.layoutRevision();
  reconciled =
      recycler.reconcile(rows, rowTweens, prototype, keys, tallRows, 0, 20, 0);
  assert(reconciled.applied && rows.layoutRevision() == oldRevision);
  assert(bounds(rows, rowId).height == 40 &&
         rows.layoutRevision() == oldRevision + 1);
}
