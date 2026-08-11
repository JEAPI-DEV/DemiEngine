#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/ui/RichTextParser.h"
#include "demi/runtime/ui/TextEditingEngine.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiAccessibilityTree.h"
#include "demi/runtime/ui/UiAccessibilityActions.h"
#include "demi/runtime/ui/UiAccessibilityBridge.h"
#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiEventQueue.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLocalization.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiPrefabResolver.h"
#include "demi/runtime/ui/UiStateController.h"
#include "demi/runtime/ui/UiTweenSystem.h"
#include "demi/runtime/ui/UiVirtualCollection.h"
#include "demi/schema/Validation.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <ranges>

using namespace demi::runtime::ui;

int main() {
  TextLayoutEngine text;
  const auto wrapped =
      text.layout({.text = "alpha beta gamma delta epsilon",
                   .width = 66.0F,
                   .fontSize = 10.0F,
                   .wrap = TextWrap::Word,
                   .horizontal = TextHorizontalAlignment::Center,
                   .overflow = TextOverflow::Ellipsis,
                   .maxLines = 2});
  if (wrapped.lines.size() != 2 || !wrapped.truncated ||
      wrapped.lines.back().text.find("...") == std::string::npos ||
      wrapped.lines.front().x <= 0.0F) {
    std::cerr << "Text wrapping, alignment, or ellipsis failed.\n";
    return 1;
  }
  const auto selection = TextLayoutEngine::selectionRects(wrapped, 1, 4);
  if (selection.empty() || selection[0].width <= 0.0F ||
      TextLayoutEngine::hitTest(wrapped, {0.0F, 0.0F}) > 1) {
    std::cerr << "Text selection geometry or hit testing failed.\n";
    return 1;
  }

  const std::string emoji = "A\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                            "e\xCC\x81";
  if (TextLayoutEngine::graphemeCount(emoji) != 3 ||
      TextLayoutEngine::graphemeSlice(emoji, 1, 1) !=
          "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" ||
      TextLayoutEngine::graphemeSlice("\xFF", 0, 1).has_value()) {
    std::cerr
        << "Unicode grapheme boundaries or invalid UTF-8 handling failed.\n";
    return 1;
  }
  const auto unicode =
      text.layout({.text = "مرحبا", .width = 100.0F, .fontSize = 12.0F});
  if (!unicode.validUtf8 || unicode.shapingComplete) {
    std::cerr << "Complex text did not expose incomplete shaping honestly.\n";
    return 1;
  }
  const auto cjk = text.layout(
      {.text = "漢字仮名交じり文",
       .width = 25.0F,
       .fontSize = 10.0F,
       .wrap = TextWrap::Word},
      [](std::string_view value) {
        return static_cast<float>(TextLayoutEngine::graphemeCount(value)) *
               10.0F;
      });
  const auto hostileSize = text.layout(
      {.text = "wide", .width = -10.0F, .fontSize = 100000.0F});
  if (cjk.lines.size() < 3 || hostileSize.lines.empty() ||
      !std::isfinite(hostileSize.width)) {
    std::cerr << "CJK fallback wrapping or hostile text sizing failed.\n";
    return 1;
  }
  const auto emptyLayout = text.layout(
      {.text = {}, .width = 100.0F, .height = 30.0F, .fontSize = 12.0F});
  if (emptyLayout.carets.size() != 1 ||
      emptyLayout.carets.front().grapheme != 0) {
    std::cerr << "Empty editable text did not expose caret geometry.\n";
    return 1;
  }
  const auto trailingSpaceLayout = TextLayoutEngine{}.layout(
      {.text = "Search ", .width = 200.0F, .fontSize = 20.0F});
  const auto trailingCaret = std::ranges::find(
      trailingSpaceLayout.carets, std::size_t{7},
      &TextLayoutResult::Caret::grapheme);
  const auto previousCaret = std::ranges::find(
      trailingSpaceLayout.carets, std::size_t{6},
      &TextLayoutResult::Caret::grapheme);
  if (trailingSpaceLayout.lines.size() != 1 ||
      trailingSpaceLayout.lines.front().text != "Search " ||
      trailingSpaceLayout.lines.front().graphemeCount != 7 ||
      trailingCaret == trailingSpaceLayout.carets.end() ||
      previousCaret == trailingSpaceLayout.carets.end() ||
      trailingCaret->x <= previousCaret->x) {
    std::cerr << "Trailing whitespace lost editable caret geometry.\n";
    return 1;
  }
  const auto whitespaceOnlyLayout = TextLayoutEngine{}.layout(
      {.text = "   ", .width = 200.0F, .fontSize = 20.0F});
  if (whitespaceOnlyLayout.lines.size() != 1 ||
      whitespaceOnlyLayout.lines.front().text != "   " ||
      whitespaceOnlyLayout.lines.front().graphemeCount != 3 ||
      whitespaceOnlyLayout.carets.size() != 4 ||
      whitespaceOnlyLayout.carets.back().grapheme != 3 ||
      whitespaceOnlyLayout.carets.back().x <=
          whitespaceOnlyLayout.carets.front().x) {
    std::cerr << "Whitespace-only editable text lost caret geometry.\n";
    return 1;
  }

  TextEditState editing;
  std::string edited = "Ae\xCC\x81"
                       "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
  TextEditingEngine::normalize(edited, editing);
  if (editing.caret != 3 || !TextEditingEngine::backspace(edited, editing) ||
      edited != "Ae\xCC\x81" ||
      !TextEditingEngine::backspace(edited, editing) || edited != "A" ||
      editing.caret != 1) {
    std::cerr << "Backspace split a Unicode grapheme cluster.\n";
    return 1;
  }
  TextEditingEngine::moveTo(editing, edited, 0);
  if (!TextEditingEngine::deleteForward(edited, editing) || !edited.empty() ||
      TextEditingEngine::deleteForward(edited, editing)) {
    std::cerr << "Forward deletion did not respect text boundaries.\n";
    return 1;
  }
  edited = "alpha";
  editing = {};
  TextEditingEngine::selectAll(editing, edited);
  if (!TextEditingEngine::insert(edited, editing, "β") || edited != "β" ||
      editing.caret != 1) {
    std::cerr << "Selection replacement failed.\n";
    return 1;
  }
  const std::string preserved = edited;
  if (TextEditingEngine::insert(edited, editing, "\xFF") ||
      edited != preserved) {
    std::cerr << "Invalid UTF-8 mutated an editable value.\n";
    return 1;
  }
  TextEditingEngine::selectAll(editing, edited);
  if (!TextEditingEngine::setComposition(editing, "候補", 1, 99) ||
      edited != "β" ||
      TextEditingEngine::displayText(edited, editing) != "候補" ||
      editing.compositionSelectionLength != 1 ||
      TextEditingEngine::displayCaret(edited, editing) != 2) {
    std::cerr
        << "IME composition did not remain separate from committed text.\n";
    return 1;
  }
  if (!TextEditingEngine::insert(edited, editing, "確定") || edited != "確定" ||
      !editing.composition.empty()) {
    std::cerr << "IME commit did not replace the selected text.\n";
    return 1;
  }
  TextEditingEngine::selectAll(editing, edited);
  TextEditingEngine::move(editing, edited, -1);
  if (editing.caret != 0 || editing.anchor != 0) {
    std::cerr << "Arrow movement did not collapse selection to its edge.\n";
    return 1;
  }
  TextLayoutCache cache(2);
  (void)cache.layout({.text = "one", .width = 20.0F});
  (void)cache.layout({.text = "one", .width = 20.0F});
  (void)cache.layout({.text = "two", .width = 20.0F});
  (void)cache.layout({.text = "three", .width = 20.0F});
  if (cache.stats().hits != 1 || cache.stats().misses != 3 ||
      cache.stats().entries != 2) {
    std::cerr << "Text layout cache was not bounded or deterministic.\n";
    return 1;
  }
  TextLayoutCache revisionCache(4);
  (void)revisionCache.layout(
      {.text = "localized", .width = 40.0F, .locale = "en", .fontRevision = 1});
  (void)revisionCache.layout(
      {.text = "localized", .width = 40.0F, .locale = "ar", .fontRevision = 1});
  (void)revisionCache.layout(
      {.text = "localized", .width = 40.0F, .locale = "ar", .fontRevision = 2});
  if (revisionCache.stats().misses != 3 || revisionCache.stats().entries != 3) {
    std::cerr << "Locale/font changes did not invalidate text layout.\n";
    return 1;
  }

  const auto rich = RichTextParser{}.parse(
      "Use [strong]safe[/strong] [link=item:1]content[/link].");
  if (rich.text != "Use safe content." || rich.spans.size() != 2 ||
      !rich.diagnostics.empty()) {
    std::cerr << "Allowlisted rich text parsing failed.\n";
    return 1;
  }
  const auto hostile = RichTextParser{}.parse("[lua=evil]x[/lua]");
  if (hostile.diagnostics.size() != 2 || hostile.text != "x") {
    std::cerr << "Executable or unknown rich tags were not rejected.\n";
    return 1;
  }

  UiDocument document;
  document.nodes = {
      {.id = "root", .type = "container"},
      {.id = "template", .parent = "root", .type = "button", .focusable = true},
      {.id = "label", .parent = "template", .type = "label"}};
  document.nodes[1].hovered = true;
  document.nodes[1].textEdit.composition = "transient";
  UiMutationQueue::initializeGenerations(document);
  const auto source = UiMutationQueue::handle(document, "template");
  if (!source)
    return 1;
  UiMutationQueue clone;
  clone.clone(*source, "row_1", "root");
  if (!clone.apply(document).applied || document.nodes.size() != 5 ||
      !UiMutationQueue::handle(document, "row_1.label") ||
      document.nodes[3].hovered ||
      !document.nodes[3].textEdit.composition.empty()) {
    std::cerr << "Transactional subtree cloning failed.\n";
    return 1;
  }

  const auto row = *UiMutationQueue::handle(document, "row_1");
  document.focusedId = row.id;
  document.pointerCaptures[0] = row.id;
  document.pointerCaptures[9] = row.id;
  UiMutationQueue remove;
  remove.remove(row);
  if (!remove.apply(document).applied || !document.focusedId.empty() ||
      !document.pointerCaptures.empty() ||
      UiMutationQueue::alive(document, row)) {
    std::cerr
        << "Removal did not cancel focus/capture or invalidate handles.\n";
    return 1;
  }

  const auto root = *UiMutationQueue::handle(document, "root");
  const std::size_t before = document.nodes.size();
  UiMutationQueue invalid;
  invalid.create("root", {.id = "valid", .type = "label"});
  invalid.create("missing", {.id = "orphan", .type = "label"});
  if (invalid.apply(document).applied || document.nodes.size() != before ||
      UiMutationQueue::handle(document, "valid")) {
    std::cerr << "Failed multi-node mutation was not transactional.\n";
    return 1;
  }
  UiMutationQueue cycle;
  cycle.reparent(root, "label");
  if (cycle.apply(document).applied) {
    std::cerr << "Cycle-producing reparent was accepted.\n";
    return 1;
  }

  UiTweenSystem tweens;
  const auto label = *UiMutationQueue::handle(document, "label");
  const auto tween =
      tweens.start(document, label, UiTweenProperty::PositionX, 100.0F, 1.0F);
  tweens.update(document, 0.5F);
  if (!tween || document.nodes[2].layout.position.x != 50.0F ||
      tweens.activeCount() != 1) {
    std::cerr << "UI tween did not update the live generation.\n";
    return 1;
  }
  UiMutationQueue removeLabel;
  removeLabel.remove(label);
  (void)removeLabel.apply(document);
  tweens.update(document, 0.1F);
  if (tweens.activeCount() != 0) {
    std::cerr << "UI tween survived target removal.\n";
    return 1;
  }
  UiDocument localized;
  localized.locales["en"] = {{"title", "Start"}};
  localized.locales["de"] = {{"title", "Starten"}};
  localized.nodes.push_back(
      {.id = "title", .type = "label", .localizationKey = "title"});
  localized.nodes[0].textEdit = {.caret = 50,
                                 .anchor = 40,
                                 .composition = "unfinished",
                                 .initialized = true};
  std::string localeError;
  if (!UiLocalization{}.setLocale(localized, "de", localeError) ||
      localized.nodes[0].text != "Starten" ||
      !localized.nodes[0].textEdit.composition.empty() ||
      localized.nodes[0].textEdit.caret != 7 ||
      UiLocalization{}.setLocale(localized, "missing", localeError) ||
      localized.nodes[0].text != "Starten") {
    std::cerr << "Locale switching did not preserve the last valid UI.\n";
    return 1;
  }
  UiLocalization{}.setPseudoLocale(localized, true);
  if (localized.nodes[0].text.front() != '[') {
    std::cerr << "Pseudo-localization did not invalidate localized text.\n";
    return 1;
  }
  UiDocument hidden;
  hidden.nodes = {{.id = "panel", .type = "panel"},
                  {.id = "control",
                   .parent = "panel",
                   .type = "button",
                   .focusable = true}};
  hidden.nodes[1].textEdit.composition = "unfinished";
  hidden.focusedId = "control";
  hidden.pointerCaptures[4] = "control";
  if (!UiStateController{}.setVisible(hidden, "panel", false) ||
      !hidden.focusedId.empty() || !hidden.pointerCaptures.empty() ||
      !hidden.nodes[1].textEdit.composition.empty()) {
    std::cerr << "Hiding an ancestor retained descendant interaction state.\n";
    return 1;
  }

  UiDocument accessibility;
  accessibility.focusedId = "music";
  accessibility.nodes = {
      {.id = "root", .type = "panel"},
      {.id = "heading",
       .parent = "root",
       .type = "label",
       .text = "Audio settings",
       .resolved = {.x = 10.0F, .y = 10.0F, .width = 200.0F, .height = 30.0F}},
      {.id = "decorative", .parent = "root", .type = "image"},
      {.id = "controls",
       .parent = "root",
       .type = "panel",
       .accessibilityLabel = "Sound controls",
       .disabled = true},
      {.id = "music",
       .parent = "controls",
       .type = "toggle",
       .text = "Music",
       .accessibilityDescription = "Play background music",
       .focusable = true,
       .checked = true},
      {.id = "hidden_group",
       .parent = "root",
       .type = "panel",
       .visible = false},
      {.id = "hidden_button",
       .parent = "hidden_group",
       .type = "button",
       .text = "Hidden",
       .focusable = true},
      {.id = "ignored_group",
       .parent = "root",
       .type = "panel",
       .accessibilityHidden = true},
      {.id = "ignored_button",
       .parent = "ignored_group",
       .type = "button",
       .text = "Ignored",
       .focusable = true},
      {.id = "editor",
       .parent = "root",
       .type = "text_input",
       .text = "Ada",
       .placeholder = "Player name",
       .resolved = {.x = std::numeric_limits<float>::quiet_NaN(),
                    .y = 70.0F,
                    .width = -10.0F,
                    .height = 24.0F},
       .disabled = true,
       .focusable = true},
      {.id = "cycle_a",
       .parent = "cycle_b",
       .type = "button",
       .text = "Cycle A",
       .focusable = true},
      {.id = "cycle_b",
       .parent = "cycle_a",
       .type = "button",
       .text = "Cycle B",
       .focusable = true},
  };
  const auto accessibilityTree = UiAccessibilityTree::snapshot(accessibility);
  const auto accessibleNode =
      [&](const std::string_view id) -> const UiAccessibilityNode * {
    const auto found =
        std::ranges::find(accessibilityTree, id, &UiAccessibilityNode::id);
    return found == accessibilityTree.end() ? nullptr : &*found;
  };
  const UiAccessibilityNode *heading = accessibleNode("heading");
  const UiAccessibilityNode *controls = accessibleNode("controls");
  const UiAccessibilityNode *music = accessibleNode("music");
  const UiAccessibilityNode *editor = accessibleNode("editor");
  if (accessibilityTree.size() != 4 || heading == nullptr ||
      controls == nullptr || music == nullptr || editor == nullptr ||
      heading->role != UiAccessibilityRole::StaticText ||
      heading->label != "Audio settings" || !heading->parent.empty() ||
      controls->role != UiAccessibilityRole::Group ||
      music->role != UiAccessibilityRole::CheckBox ||
      music->parent != "controls" || !music->focused || !music->disabled ||
      !music->checked || music->description != "Play background music" ||
      editor->role != UiAccessibilityRole::TextField ||
      editor->label != "Player name" || editor->valueText != "Ada" ||
      !editor->disabled || editor->bounds.x != 0.0F ||
      editor->bounds.width != 0.0F || accessibleNode("decorative") != nullptr ||
      accessibleNode("hidden_button") != nullptr ||
      accessibleNode("ignored_button") != nullptr ||
      accessibleNode("cycle_a") != nullptr) {
    std::cerr << "Accessibility snapshot semantics or filtering failed.\n";
    return 1;
  }

  UiDocument clippedAccessibility;
  clippedAccessibility.nodes = {
      {.id = "scroll",
       .type = "scroll",
       .resolved = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 50.0F}},
      {.id = "offscreen",
       .parent = "scroll",
       .type = "button",
       .text = "Later item",
       .resolved = {.x = 0.0F, .y = 80.0F, .width = 100.0F, .height = 20.0F},
       .focusable = true},
  };
  const auto clippedTree = UiAccessibilityTree::snapshot(clippedAccessibility);
  if (clippedTree.size() != 2 || clippedTree[1].id != "offscreen" ||
      clippedTree[1].parent != "scroll" || !clippedTree[1].offscreen ||
      clippedTree[1].bounds.height != 0.0F) {
    std::cerr << "Accessibility scroll clipping state failed.\n";
    return 1;
  }

  UiDocument duplicateAccessibility;
  duplicateAccessibility.nodes = {
      {.id = "duplicate", .type = "button", .text = "Hidden", .visible = false},
      {.id = "duplicate", .type = "button", .text = "Visible"},
  };
  if (!UiAccessibilityTree::snapshot(duplicateAccessibility).empty()) {
    std::cerr << "Accessibility duplicate IDs did not use the first node as "
                 "canonical.\n";
    return 1;
  }

  const UiDocument parsedAccessibility = parseUiDocument(nlohmann::json::parse(
      R"({"root":{"id":"probe","type":"button","font":"asset://fonts/roboto","accessibility_label":"Run","accessibility_description":"Starts the probe","accessibility_hidden":true}})"));
  if (parsedAccessibility.nodes.size() != 1 ||
      parsedAccessibility.nodes[0].accessibilityLabel != "Run" ||
      parsedAccessibility.nodes[0].accessibilityDescription !=
          "Starts the probe" ||
      parsedAccessibility.nodes[0].font != "asset://fonts/roboto" ||
      !parsedAccessibility.nodes[0].accessibilityHidden) {
    std::cerr << "Accessibility HUD metadata did not parse.\n";
    return 1;
  }

  const auto range =
      UiVirtualCollection::visibleRange(10000, 20.0F, 1000.0F, 200.0F, 3);
  if (range.first != 47 || range.count != 16 || range.count >= 10000 ||
      UiVirtualCollection::visibleRange(10, 0.0F, 0.0F, 100.0F).count != 0 ||
      UiVirtualCollection::visibleRange(4, 10.0F, 0.0F, 10.0F,
                                        std::numeric_limits<std::size_t>::max())
              .count != 4) {
    std::cerr << "Bounded virtual collection range failed.\n";
    return 1;
  }

  UiVirtualLayout variableLayout;
  std::string virtualError;
  const std::array variableExtents{10.0F, 30.0F, 20.0F, 40.0F};
  if (!variableLayout.reset(variableExtents, virtualError) ||
      variableLayout.itemCount() != 4 ||
      variableLayout.totalExtent() != 100.0F ||
      variableLayout.itemOffset(1) != 10.0F ||
      variableLayout.itemExtent(1) != 30.0F ||
      variableLayout.visibleRange(10.0F, 30.0F, 0).first != 1 ||
      variableLayout.visibleRange(10.0F, 30.0F, 0).count != 1 ||
      variableLayout.visibleRange(10.0F, 30.0F, 1).first != 0 ||
      variableLayout.visibleRange(10.0F, 30.0F, 1).count != 3 ||
      variableLayout.visibleRange(5.0F, 10.0F, 0).count != 2 ||
      variableLayout
              .visibleRange(5.0F, 10.0F,
                            std::numeric_limits<std::size_t>::max())
              .count != 4 ||
      variableLayout.visibleRange(100.0F, 10.0F).first != 4 ||
      variableLayout.visibleRange(100.0F, 10.0F).count != 0) {
    std::cerr << "Variable-height virtual collection range failed.\n";
    return 1;
  }
  UiVirtualLayout largeVariableLayout;
  const std::array largeExtents{1.0e20F, 1.0e20F};
  if (!largeVariableLayout.reset(largeExtents, virtualError) ||
      largeVariableLayout.visibleRange(1.0e20F, 1.0F, 0).first != 1 ||
      largeVariableLayout.visibleRange(1.0e20F, 1.0F, 0).count != 1) {
    std::cerr << "Large variable-height range lost its visible boundary row.\n";
    return 1;
  }
  const std::array invalidExtents{10.0F,
                                  std::numeric_limits<float>::quiet_NaN()};
  const std::array impreciseExtents{std::numeric_limits<float>::max() * 0.5F,
                                    1.0F};
  if (variableLayout.reset(invalidExtents, virtualError) ||
      virtualError.empty() || variableLayout.totalExtent() != 100.0F ||
      !variableLayout.setItemExtent(0, 25.0F, virtualError) ||
      variableLayout.itemOffset(1) != 25.0F ||
      variableLayout.totalExtent() != 115.0F ||
      variableLayout.setItemExtent(4, 10.0F, virtualError) ||
      variableLayout.setItemExtent(0, 0.0F, virtualError) ||
      variableLayout.reset(impreciseExtents, virtualError) ||
      variableLayout.totalExtent() != 115.0F ||
      variableLayout
              .visibleRange(std::numeric_limits<float>::quiet_NaN(), 25.0F, 0)
              .first != 0 ||
      variableLayout.visibleRange(0.0F, -1.0F).count != 0) {
    std::cerr << "Variable-height virtual collection mutation safety failed.\n";
    return 1;
  }

  UiDocument eventsDocument;
  eventsDocument.nodes = {
      {.id = "drag_source",
       .type = "slider",
       .resolved = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
       .maximum = 100.0F,
       .focusable = true},
      {.id = "drop_target",
       .type = "button",
       .resolved = {.x = 120.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
       .focusable = true},
  };
  UiInteractionController eventInteraction;
  if (!eventInteraction.movePointer(eventsDocument, 0, {10.0F, 10.0F},
                                    "mouse") ||
      eventInteraction.movePointer(eventsDocument, 0, {11.0F, 10.0F},
                                   "mouse") ||
      !eventInteraction.capturePointer(eventsDocument, 0, {10.0F, 10.0F},
                                       "mouse") ||
      !eventInteraction.updatePointer(eventsDocument, 0, {40.0F, 10.0F},
                                      "mouse") ||
      !eventInteraction.updatePointer(eventsDocument, 0, {140.0F, 10.0F},
                                      "mouse") ||
      !eventInteraction.scrollPointer(eventsDocument, 0, {140.0F, 10.0F},
                                      {0.0F, -2.0F}, "mouse")) {
    std::cerr << "Typed pointer interaction setup failed.\n";
    return 1;
  }
  eventInteraction.releasePointer(eventsDocument, 0, {140.0F, 10.0F}, false,
                                  "mouse");
  const auto pointerEvents = UiEventQueue::take(eventsDocument);
  const auto hasEvent = [&](const UiEventType type, const std::string_view id) {
    return std::ranges::any_of(pointerEvents, [&](const UiEvent &event) {
      return event.type == type && event.id == id;
    });
  };
  if (!hasEvent(UiEventType::PointerEnter, "drag_source") ||
      !hasEvent(UiEventType::FocusGained, "drag_source") ||
      !hasEvent(UiEventType::Press, "drag_source") ||
      !hasEvent(UiEventType::DragStart, "drag_source") ||
      !hasEvent(UiEventType::Drag, "drag_source") ||
      !hasEvent(UiEventType::ValueChanged, "drag_source") ||
      !hasEvent(UiEventType::PointerExit, "drag_source") ||
      !hasEvent(UiEventType::PointerEnter, "drop_target") ||
      !hasEvent(UiEventType::Scroll, "drop_target") ||
      !hasEvent(UiEventType::Release, "drag_source") ||
      !hasEvent(UiEventType::DragEnd, "drag_source") ||
      !hasEvent(UiEventType::Drop, "drop_target") ||
      !UiEventQueue::take(eventsDocument).empty()) {
    std::cerr
        << "Typed UI pointer event sequence was incomplete or repeated.\n";
    return 1;
  }
  eventsDocument.events.clear();
  if (!eventInteraction.capturePointer(eventsDocument, 3, {10.0F, 10.0F},
                                       "touch") ||
      !eventInteraction.updatePointer(eventsDocument, 3, {30.0F, 10.0F},
                                      "touch")) {
    return 1;
  }
  UiEventQueue::cancelSubtree(eventsDocument, "drag_source", "node_removed");
  const auto cancelledEvents = UiEventQueue::take(eventsDocument);
  const auto cancelledCount = static_cast<std::size_t>(
      std::ranges::count_if(cancelledEvents, [](const UiEvent &event) {
        return event.type == UiEventType::Cancel && event.id == "drag_source";
      }));
  if (eventInteraction.pointerCaptured(eventsDocument, 3) ||
      cancelledCount != 1 ||
      std::ranges::none_of(cancelledEvents,
                           [](const UiEvent &event) {
                             return event.type == UiEventType::Cancel &&
                                    event.id == "drag_source" &&
                                    event.cancelled;
                           }) ||
      std::ranges::none_of(cancelledEvents,
                           [](const UiEvent &event) {
                             return event.type == UiEventType::DragEnd &&
                                    event.id == "drag_source" &&
                                    event.cancelled;
                           }) ||
      std::ranges::none_of(cancelledEvents, [](const UiEvent &event) {
        return event.type == UiEventType::PointerExit &&
               event.id == "drag_source" && event.cancelled;
      })) {
    std::cerr << "Removing a captured UI subtree did not cancel interaction.\n";
    return 1;
  }

  UiDocument multiPointer;
  multiPointer.nodes = {
      {.id = "touch_target",
       .type = "button",
       .resolved = {.x = 0.0F, .y = 0.0F, .width = 50.0F, .height = 50.0F},
       .focusable = true}};
  (void)eventInteraction.movePointer(multiPointer, 1, {10.0F, 10.0F}, "touch");
  (void)eventInteraction.movePointer(multiPointer, 2, {20.0F, 20.0F}, "touch");
  (void)eventInteraction.movePointer(multiPointer, 1, {80.0F, 80.0F}, "touch");
  if (!multiPointer.nodes[0].hovered) {
    std::cerr << "One touch exit cleared another touch's hover state.\n";
    return 1;
  }
  (void)eventInteraction.movePointer(multiPointer, 2, {80.0F, 80.0F}, "touch");
  if (multiPointer.nodes[0].hovered) {
    std::cerr << "Final touch exit retained stale hover state.\n";
    return 1;
  }

  UiDocument keyboardEvents;
  keyboardEvents.nodes = {
      {.id = "toggle",
       .type = "toggle",
       .action = "toggle_setting",
       .focusable = true},
      {.id = "editor", .type = "text_input", .focusable = true}};
  if (!eventInteraction.focusNext(keyboardEvents) ||
      !eventInteraction.activateFocused(keyboardEvents, "keyboard") ||
      !eventInteraction.focusNext(keyboardEvents)) {
    std::cerr << "Keyboard UI event setup failed.\n";
    return 1;
  }
  (void)eventInteraction.activateFocused(keyboardEvents, "mouse");
  const auto keyboardEventBatch = UiEventQueue::take(keyboardEvents);
  const auto eventCount = [&](const UiEventType type,
                              const std::string_view id) {
    return std::ranges::count_if(keyboardEventBatch, [&](const UiEvent &event) {
      return event.type == type && event.id == id;
    });
  };
  if (!keyboardEvents.nodes[0].checked ||
      eventCount(UiEventType::ValueChanged, "toggle") != 1 ||
      eventCount(UiEventType::Submit, "toggle") != 1 ||
      eventCount(UiEventType::FocusLost, "toggle") != 1 ||
      eventCount(UiEventType::FocusGained, "editor") != 1 ||
      eventCount(UiEventType::Submit, "editor") != 0) {
    std::cerr << "Typed keyboard, toggle, or text-input events were wrong.\n";
    return 1;
  }

  UiDocument scrollEvents;
  scrollEvents.nodes = {
      {.id = "scroll_view",
       .type = "scroll",
       .resolved = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F}},
      {.id = "scroll_label",
       .parent = "scroll_view",
       .type = "label",
       .resolved = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 30.0F}},
  };
  if (!eventInteraction.scrollPointer(scrollEvents, 0, {10.0F, 10.0F},
                                      {0.0F, -1.0F}, "mouse")) {
    std::cerr << "Non-focusable scroll container rejected wheel input.\n";
    return 1;
  }
  const auto scrollEventBatch = UiEventQueue::take(scrollEvents);
  if (scrollEventBatch.size() != 1 ||
      scrollEventBatch[0].type != UiEventType::Scroll ||
      scrollEventBatch[0].id != "scroll_view") {
    std::cerr << "Wheel input did not route to the nearest scroll ancestor.\n";
    return 1;
  }

  const std::filesystem::path prefabRoot =
      std::filesystem::temp_directory_path() / "demi_ui_prefab_step3";
  std::filesystem::remove_all(prefabRoot);
  std::filesystem::create_directories(prefabRoot / "ui");
  std::filesystem::create_directories(prefabRoot / "scenes");
  const auto write = [](const std::filesystem::path &path,
                        const std::string_view content) {
    std::ofstream output(path);
    output << content;
    return output.good();
  };
  if (!write(prefabRoot / "demi.project.json", R"({"format_version":1})") ||
      !write(prefabRoot / "ui/badge.ui.prefab.json", R"({
        "format_version": 1,
        "id": "ui-prefab://badge",
        "parameters": {"caption": {"type": "string", "default": "NEW"}},
        "root": {"id": "badge", "type": "panel", "children": [
          {"id": "caption", "type": "label", "text": "${caption}"}
        ]}
      })") ||
      !write(prefabRoot / "ui/button.ui.prefab.json", R"({
        "format_version": 1,
        "id": "ui-prefab://button",
        "parameters": {
          "label": {"type": "string"},
          "size": {"type": "number", "default": 20}
        },
        "root": {"id": "root", "type": "button", "text": "Open ${label}",
          "font_size": "${size}", "children": [
            {"id": "label", "type": "label", "text": "${label}"},
            {"id": "status", "prefab": "ui-prefab://badge", "arguments": {}}
          ]}
      })")) {
    std::cerr << "Could not create UI prefab fixtures.\n";
    return 1;
  }
  const std::filesystem::path hudPath = prefabRoot / "scenes/test.hud.json";
  const nlohmann::json prefabHud = nlohmann::json::parse(R"({
    "format_version": 1,
    "root": {"id": "menu", "type": "panel", "children": [
      {"id": "confirm", "prefab": "ui-prefab://button",
       "arguments": {"label": "Settings", "size": 24}}
    ]}
  })");
  const auto expandedPrefab = expandUiDocument(hudPath, prefabHud);
  if (!expandedPrefab.document || !expandedPrefab.diagnostics.empty()) {
    std::cerr << "Valid parameterized UI prefab did not expand.\n";
    return 1;
  }
  const UiDocument prefabDocument = parseUiDocument(*expandedPrefab.document);
  const auto confirm =
      std::ranges::find(prefabDocument.nodes, "confirm", &UiNode::id);
  const auto nested = std::ranges::find(prefabDocument.nodes,
                                        "confirm.status.caption", &UiNode::id);
  if (confirm == prefabDocument.nodes.end() ||
      confirm->text != "Open Settings" || confirm->fontSize != 24.0F ||
      nested == prefabDocument.nodes.end() || nested->text != "NEW") {
    std::cerr << "UI prefab parameters or stable nested ids were incorrect.\n";
    return 1;
  }
  nlohmann::json invalidArguments = prefabHud;
  invalidArguments["root"]["children"][0]["arguments"] = {{"label", 7},
                                                          {"unknown", true}};
  const auto rejectedArguments = expandUiDocument(hudPath, invalidArguments);
  if (rejectedArguments.document || rejectedArguments.diagnostics.size() < 2) {
    std::cerr
        << "Invalid UI prefab arguments were not rejected transactionally.\n";
    return 1;
  }
  nlohmann::json ambiguousNode = prefabHud;
  ambiguousNode["root"]["children"][0]["type"] = "button";
  const auto rejectedAmbiguousNode = expandUiDocument(hudPath, ambiguousNode);
  if (rejectedAmbiguousNode.document ||
      std::ranges::none_of(rejectedAmbiguousNode.diagnostics,
                           [](const auto &item) {
                             return item.code == "UI_PREFAB_NODE_AMBIGUOUS";
                           })) {
    std::cerr << "Ambiguous typed/prefab UI node was not rejected.\n";
    return 1;
  }
  if (!write(prefabRoot / "ui/bad_version.ui.prefab.json", R"({
        "format_version":"one","id":"ui-prefab://bad_version",
        "root":{"id":"root","type":"panel"}
      })")) {
    return 1;
  }
  const auto malformedVersion =
      inspectUiPrefab(prefabRoot / "ui/bad_version.ui.prefab.json");
  if (malformedVersion.document ||
      std::ranges::none_of(malformedVersion.diagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_DOCUMENT_INVALID";
      })) {
    std::cerr << "Malformed UI prefab version did not fail safely.\n";
    return 1;
  }
  if (!write(prefabRoot / "ui/cycle_a.ui.prefab.json", R"({
        "format_version":1,"id":"ui-prefab://cycle_a",
        "root":{"id":"b","prefab":"ui-prefab://cycle_b","arguments":{}}
      })") ||
      !write(prefabRoot / "ui/cycle_b.ui.prefab.json", R"({
        "format_version":1,"id":"ui-prefab://cycle_b",
        "root":{"id":"a","prefab":"ui-prefab://cycle_a","arguments":{}}
      })")) {
    return 1;
  }
  nlohmann::json cyclicHud = prefabHud;
  cyclicHud["root"]["children"] =
      nlohmann::json::array({{{"id", "cycle"},
                              {"prefab", "ui-prefab://cycle_a"},
                              {"arguments", nlohmann::json::object()}}});
  const auto rejectedCycle = expandUiDocument(hudPath, cyclicHud);
  if (rejectedCycle.document ||
      std::ranges::none_of(
          rejectedCycle.diagnostics,
          [](const auto &item) { return item.code == "UI_PREFAB_CYCLE"; }) ||
      resolveUiPrefabReference(hudPath, "ui-prefab://../outside")) {
    std::cerr << "UI prefab cycle or traversal protection failed.\n";
    return 1;
  }
  if (!demi::isUiPrefabFile(prefabRoot / "ui/button.ui.prefab.json") ||
      demi::isPrefabFile(prefabRoot / "ui/button.ui.prefab.json") ||
      demi::classifySourceFile(prefabRoot / "ui/button.ui.prefab.json") !=
          demi::SourceFileKind::UiPrefab) {
    std::cerr << "UI prefab source classification was ambiguous.\n";
    return 1;
  }
  const auto validPrefabDiagnostics = demi::validateTextFile(
      prefabRoot / "ui/button.ui.prefab.json", demi::SourceFileKind::UiPrefab);
  if (demi::hasErrors(validPrefabDiagnostics)) {
    std::cerr << "Valid UI prefab failed CLI-source validation.\n";
    return 1;
  }
  invalidArguments["format_version"] = 1;
  if (!write(hudPath, invalidArguments.dump(2))) {
    return 1;
  }
  const auto invalidHudDiagnostics =
      demi::validateTextFile(hudPath, demi::SourceFileKind::Hud);
  if (std::ranges::none_of(invalidHudDiagnostics,
                           [](const auto &item) {
                             return item.code == "UI_PREFAB_ARGUMENT_UNKNOWN";
                           }) ||
      std::ranges::none_of(invalidHudDiagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_ARGUMENT_TYPE";
      })) {
    std::cerr << "HUD validation did not surface UI prefab argument errors.\n";
    return 1;
  }

  // Stable-key row recycling keeps the live tree bounded and invalidates all
  // transient state before a slot represents different game data.
  UiDocument recycled;
  recycled.nodes = {
      {.id = "rows", .type = "panel"},
      {.id = "row_template",
       .parent = "rows",
       .type = "button",
       .text = "template",
       .visible = false,
       .focusable = true},
      {.id = "row_label",
       .parent = "row_template",
       .type = "label",
       .text = "template label"}};
  UiMutationQueue::initializeGenerations(recycled);
  const auto rowTemplate = UiMutationQueue::handle(recycled, "row_template");
  UiTweenSystem recycleTweens;
  UiVirtualRecycler recycler("probe_rows");
  std::vector<std::string> stableKeys;
  std::vector<float> extents(10000, 10.0F);
  stableKeys.reserve(extents.size());
  for (std::size_t index = 0; index < extents.size(); ++index)
    stableKeys.push_back("key_" + std::to_string(index));
  auto visibleRows = recycler.reconcile(recycled, recycleTweens, *rowTemplate,
                                        stableKeys, extents, 0.0F, 30.0F, 1);
  if (!visibleRows.applied || visibleRows.rows.size() != 4 ||
      recycler.poolSize() != 4 || recycled.nodes.size() != 11) {
    std::cerr << "Virtual row pool was not bounded by the visible range.\n";
    return 1;
  }
  const UiNodeHandle staleRow = visibleRows.rows.front().node;
  UiNode *dirty = UiStateController{}.find(recycled, staleRow.id);
  UiNode *dirtyChild =
      UiStateController{}.find(recycled, staleRow.id + ".row_label");
  dirty->hovered = true;
  dirty->textEdit = {.caret = 4, .anchor = 1, .composition = "pending"};
  dirtyChild->text = "runtime binding";
  UiMutationQueue runtimeChild;
  runtimeChild.create(staleRow.id,
                      {.id = staleRow.id + ".runtime_child", .type = "label"});
  if (!runtimeChild.apply(recycled).applied) {
    std::cerr << "Could not establish runtime recycler child probe.\n";
    return 1;
  }
  recycled.focusedId = staleRow.id;
  recycled.pointerCaptures[9] = staleRow.id;
  recycled.pointerHoverIds[9] = staleRow.id;
  recycled.draggingPointers.insert(9);
  if (!recycleTweens.start(recycled, staleRow, UiTweenProperty::Scale, 2.0F,
                           10.0F)) {
    std::cerr << "Could not establish recycler transient-state probe.\n";
    return 1;
  }
  visibleRows = recycler.reconcile(recycled, recycleTweens, *rowTemplate,
                                   stableKeys, extents, 500.0F, 30.0F, 1);
  dirty = UiStateController{}.find(recycled, staleRow.id);
  dirtyChild = UiStateController{}.find(recycled, staleRow.id + ".row_label");
  if (!visibleRows.applied || recycled.focusedId == staleRow.id ||
      recycled.pointerCaptures.contains(9) ||
      recycled.pointerHoverIds.contains(9) ||
      recycled.draggingPointers.contains(9) ||
      recycleTweens.activeCount() != 0 ||
      UiMutationQueue::alive(recycled, staleRow) || dirty->hovered ||
      !dirty->textEdit.composition.empty() ||
      dirtyChild->text != "template label" ||
      UiStateController{}.find(recycled, staleRow.id + ".runtime_child") !=
          nullptr) {
    std::cerr << "Recycled row retained focus, capture, tween, or binding state.\n";
    return 1;
  }
  const std::string retainedKey = visibleRows.rows[1].key;
  const std::string retainedNode = visibleRows.rows[1].node.id;
  recycled.focusedId = retainedNode;
  std::swap(stableKeys[visibleRows.rows[0].index],
            stableKeys[visibleRows.rows[1].index]);
  visibleRows = recycler.reconcile(recycled, recycleTweens, *rowTemplate,
                                   stableKeys, extents, 500.0F, 30.0F, 1);
  const auto retained = std::ranges::find(
      visibleRows.rows, retainedKey, &UiVirtualRowBinding::key);
  if (!visibleRows.applied || retained == visibleRows.rows.end() ||
      retained->node.id != retainedNode || recycled.focusedId != retainedNode) {
    std::cerr << "Stable row key did not retain its node and focus after reorder.\n";
    return 1;
  }
  const std::size_t boundedNodeCount = recycled.nodes.size();
  for (std::size_t cycle = 0; cycle < 250; ++cycle) {
    const float offset = static_cast<float>((cycle * 37) % 9900) * 10.0F;
    if (!recycler.reconcile(recycled, recycleTweens, *rowTemplate, stableKeys,
                            extents, offset, 30.0F, 1)
             .applied ||
        recycled.nodes.size() != boundedNodeCount) {
      std::cerr << "Repeated row recycling grew or corrupted the live tree.\n";
      return 1;
    }
  }
  std::vector<std::string> duplicateKeys = stableKeys;
  duplicateKeys[1] = duplicateKeys[0];
  const auto rejectedRows = recycler.reconcile(
      recycled, recycleTweens, *rowTemplate, duplicateKeys, extents, 0.0F,
      30.0F, 1);
  if (rejectedRows.applied || recycled.nodes.size() != boundedNodeCount) {
    std::cerr << "Duplicate virtual keys were not rejected transactionally.\n";
    return 1;
  }
  UiMutationQueue externalRemoval;
  externalRemoval.remove(
      *UiMutationQueue::handle(recycled, visibleRows.rows.front().node.id));
  if (!externalRemoval.apply(recycled).applied || recycler.valid(recycled)) {
    std::cerr << "Externally removed virtual slots were not detected.\n";
    return 1;
  }
  recycler.clear(recycled, recycleTweens);
  if (recycler.poolSize() != 0 || recycled.nodes.size() != 3) {
    std::cerr << "Virtual recycler teardown retained pooled nodes.\n";
    return 1;
  }

  UiDocument actionable;
  actionable.nodes = {
      {.id = "play", .type = "button", .action = "play", .focusable = true},
      {.id = "music",
       .type = "toggle",
       .action = "music",
       .focusable = true},
      {.id = "volume",
       .type = "slider",
       .value = 5.0F,
       .minimum = 0.0F,
       .maximum = 10.0F,
       .focusable = true},
      {.id = "name", .type = "text_input", .focusable = true},
      {.id = "list",
       .type = "scroll",
       .resolved = {.width = 100.0F, .height = 80.0F}},
      {.id = "hidden",
       .type = "button",
       .visible = false,
       .focusable = true}};
  UiAccessibilityActions accessibilityActions;
  if (!accessibilityActions
           .perform(actionable, {.type = UiAccessibilityActionType::Focus,
                                 .nodeId = "play"})
           .handled ||
      actionable.focusedId != "play") {
    std::cerr << "Accessibility focus action failed.\n";
    return 1;
  }
  const auto activation = accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::Activate, .nodeId = "play"});
  static_cast<void>(accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::Activate, .nodeId = "music"}));
  static_cast<void>(accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::SetValue,
       .nodeId = "volume",
       .value = 50.0F}));
  static_cast<void>(accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::SetText,
       .nodeId = "name",
       .text = "Zoë"}));
  static_cast<void>(accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::ScrollForward, .nodeId = "list"}));
  const auto hiddenAction = accessibilityActions.perform(
      actionable,
      {.type = UiAccessibilityActionType::Activate, .nodeId = "hidden"});
  if (!activation.handled || activation.action != "play" ||
      !actionable.nodes[1].checked || actionable.nodes[2].value != 10.0F ||
      actionable.nodes[3].text != "Zoë" || hiddenAction.handled ||
      actionable.events.size() < 7) {
    std::cerr << "Accessibility actions did not use normal UI state/events.\n";
    return 1;
  }
  BufferedUiAccessibilityBridge bridge;
  UiAccessibilityBridgeController bridgeController(bridge);
  bridge.pushAction(
      {.type = UiAccessibilityActionType::Focus, .nodeId = "music"});
  bridgeController.update(actionable);
  if (actionable.focusedId != "music" || bridge.nodes().empty() ||
      bridge.revision() != 1 || bridgeController.revision() != 1) {
    std::cerr << "Accessibility bridge did not exchange snapshots/actions.\n";
    return 1;
  }
  auto &platformBridge = platformUiAccessibilityBridge();
  platformBridge.clear();
  platformBridge.submitAction(
      {.type = UiAccessibilityActionType::Focus, .nodeId = "play"});
  UiAccessibilityBridgeController platformController(platformBridge);
  platformController.update(actionable);
  if (actionable.focusedId != "play" || platformBridge.snapshot().empty() ||
      platformBridge.revision() != 1) {
    std::cerr << "Thread-safe platform accessibility bridge failed.\n";
    return 1;
  }
  platformBridge.clear();
  if (!platformBridge.snapshot().empty()) {
    std::cerr << "Platform accessibility teardown retained stale nodes.\n";
    return 1;
  }
  std::filesystem::remove_all(prefabRoot);
  return 0;
}
