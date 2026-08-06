#include "demi/runtime/ui/RichTextParser.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiVirtualCollection.h"
#include "demi/runtime/ui/UiTweenSystem.h"
#include "demi/runtime/ui/UiLocalization.h"
#include "demi/runtime/ui/UiStateController.h"

#include <iostream>

using namespace demi::runtime::ui;

int main() {
  TextLayoutEngine text;
  const auto wrapped = text.layout({.text = "alpha beta gamma delta epsilon",
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
    std::cerr << "Unicode grapheme boundaries or invalid UTF-8 handling failed.\n";
    return 1;
  }
  const auto unicode = text.layout({.text = "مرحبا", .width = 100.0F,
                                    .fontSize = 12.0F});
  if (!unicode.validUtf8 || unicode.shapingComplete) {
    std::cerr << "Complex text did not expose incomplete shaping honestly.\n";
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
  document.nodes = {{.id = "root", .type = "container"},
                    {.id = "template", .parent = "root", .type = "button",
                     .focusable = true},
                    {.id = "label", .parent = "template", .type = "label"}};
  UiMutationQueue::initializeGenerations(document);
  const auto source = UiMutationQueue::handle(document, "template");
  if (!source) return 1;
  UiMutationQueue clone;
  clone.clone(*source, "row_1", "root");
  if (!clone.apply(document).applied || document.nodes.size() != 5 ||
      !UiMutationQueue::handle(document, "row_1.label")) {
    std::cerr << "Transactional subtree cloning failed.\n";
    return 1;
  }

  const auto row = *UiMutationQueue::handle(document, "row_1");
  document.focusedId = row.id;
  document.pointerCaptureId = row.id;
  document.pointerCaptures[9] = row.id;
  UiMutationQueue remove;
  remove.remove(row);
  if (!remove.apply(document).applied || !document.focusedId.empty() ||
      !document.pointerCaptureId.empty() || !document.pointerCaptures.empty() ||
      UiMutationQueue::alive(document, row)) {
    std::cerr << "Removal did not cancel focus/capture or invalidate handles.\n";
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
  const auto tween = tweens.start(document, label, UiTweenProperty::PositionX,
                                  100.0F, 1.0F);
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
  localized.nodes.push_back({.id = "title", .type = "label",
                             .localizationKey = "title"});
  std::string localeError;
  if (!UiLocalization{}.setLocale(localized, "de", localeError) ||
      localized.nodes[0].text != "Starten" ||
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
                  {.id = "control", .parent = "panel", .type = "button",
                   .focusable = true}};
  hidden.focusedId = "control";
  hidden.pointerCaptures[4] = "control";
  if (!UiStateController{}.setVisible(hidden, "panel", false) ||
      !hidden.focusedId.empty() || !hidden.pointerCaptures.empty()) {
    std::cerr << "Hiding an ancestor retained descendant interaction state.\n";
    return 1;
  }

  const auto range = UiVirtualCollection::visibleRange(10000, 20.0F, 1000.0F,
                                                        200.0F, 3);
  if (range.first != 47 || range.count != 16 || range.count >= 10000 ||
      UiVirtualCollection::visibleRange(10, 0.0F, 0.0F, 100.0F).count != 0) {
    std::cerr << "Bounded virtual collection range failed.\n";
    return 1;
  }
  return 0;
}
