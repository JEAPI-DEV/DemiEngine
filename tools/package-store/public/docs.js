/* Documentation enhancements: Lua/JSON highlighting and navigation state. */
(function () {
  "use strict";

  var LUA_KEYWORDS = {
    and: 1, break: 1, do: 1, else: 1, elseif: 1, end: 1, false: 1, for: 1,
    function: 1, goto: 1, if: 1, in: 1, local: 1, nil: 1, not: 1, or: 1,
    repeat: 1, return: 1, then: 1, true: 1, until: 1, while: 1
  };
  var JSON_KEYWORDS = { true: 1, false: 1, null: 1 };

  function escapeHtml(text) {
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function highlightLua(source) {
    var out = "";
    var i = 0;
    var n = source.length;
    while (i < n) {
      var rest = source.slice(i);
      var comment = rest.match(/^--\[\[[\s\S]*?\]\]|^--[^\n]*/);
      if (comment) {
        out += '<span class="tok-comment">' + escapeHtml(comment[0]) + "</span>";
        i += comment[0].length;
        continue;
      }
      var longString = rest.match(/^\[(=*)\[[\s\S]*?\]\1\]/);
      if (longString) {
        out += '<span class="tok-string">' + escapeHtml(longString[0]) + "</span>";
        i += longString[0].length;
        continue;
      }
      var ch = source[i];
      if (ch === '"' || ch === "'") {
        var j = i + 1;
        while (j < n) {
          if (source[j] === "\\") { j += 2; continue; }
          if (source[j] === ch || source[j] === "\n") { j += 1; break; }
          j += 1;
        }
        out += '<span class="tok-string">' + escapeHtml(source.slice(i, j)) + "</span>";
        i = j;
        continue;
      }
      var number = rest.match(/^0[xX][0-9a-fA-F]+|^\d+(\.\d+)?([eE][+-]?\d+)?/);
      if (number && /[\d.]/.test(ch)) {
        out += '<span class="tok-number">' + escapeHtml(number[0]) + "</span>";
        i += number[0].length;
        continue;
      }
      var word = rest.match(/^[A-Za-z_][A-Za-z0-9_]*/);
      if (word) {
        if (LUA_KEYWORDS[word[0]]) {
          out += '<span class="tok-keyword">' + escapeHtml(word[0]) + "</span>";
        } else if (/^[A-Z][A-Za-z0-9_]*$/.test(word[0]) && source[i + word[0].length] === ":") {
          out += '<span class="tok-type">' + escapeHtml(word[0]) + "</span>";
        } else if (/^(Input|Entity|Scene|Prefab|Hud|Events|Data|Save|Assets|Time|Timer|Transform2D|Transform3D|Sprite2D|Tilemap2D|Animation|Camera3D|Grid|Navigation2D|Audio|Profile|Debug|Test|Random|Vec2|Vec3|Body|Q2|Q3|Character|Destruction|Http|NetworkSession|Network|Source|Deform|Proc|Nav|SpriteAnimator|LuaLS)$/.test(word[0])) {
          out += '<span class="tok-api">' + escapeHtml(word[0]) + "</span>";
        } else {
          out += escapeHtml(word[0]);
        }
        i += word[0].length;
        continue;
      }
      out += escapeHtml(ch);
      i += 1;
    }
    return out;
  }

  function highlightJson(source) {
    var out = "";
    var i = 0;
    var n = source.length;
    while (i < n) {
      var ch = source[i];
      if (ch === '"') {
        var j = i + 1;
        while (j < n) {
          if (source[j] === "\\") { j += 2; continue; }
          if (source[j] === '"') { j += 1; break; }
          j += 1;
        }
        var literal = source.slice(i, j);
        var k = i;
        while (k > 0 && (source[k - 1] === " " || source[k - 1] === "\t" || source[k - 1] === "\n")) { k -= 1; }
        var isKey = k > 0 && (source[k - 1] === "{" || source[k - 1] === ",");
        out += '<span class="' + (isKey ? "tok-key" : "tok-string") + '">' + escapeHtml(literal) + "</span>";
        i = j;
        continue;
      }
      var rest2 = source.slice(i);
      var number2 = rest2.match(/^-?\d+(\.\d+)?([eE][+-]?\d+)?/);
      if (number2 && (ch === "-" || (ch >= "0" && ch <= "9"))) {
        out += '<span class="tok-number">' + escapeHtml(number2[0]) + "</span>";
        i += number2[0].length;
        continue;
      }
      var word2 = rest2.match(/^[A-Za-z_][A-Za-z0-9_]*/);
      if (word2) {
        out += JSON_KEYWORDS[word2[0]]
          ? '<span class="tok-keyword">' + escapeHtml(word2[0]) + "</span>"
          : escapeHtml(word2[0]);
        i += word2[0].length;
        continue;
      }
      var comment2 = rest2.match(/^\/\/[^\n]*|^#[^\n]*/);
      if (comment2) {
        out += '<span class="tok-comment">' + escapeHtml(comment2[0]) + "</span>";
        i += comment2[0].length;
        continue;
      }
      out += escapeHtml(ch);
      i += 1;
    }
    return out;
  }

  function looksLikeLua(source) {
    return /(^|\n)\s*(local |function |end\b|return |if\b|for\b|while\b|---@|require\()/.test(source) ||
      /function\s+\w+[.:]\w+/.test(source);
  }

  function highlightAll() {
    var blocks = document.querySelectorAll(".docs-article pre > code, .docs-article pre code");
    Array.prototype.forEach.call(blocks, function (code) {
      if (code.getAttribute("data-highlighted") === "1") { return; }
      var text = code.textContent;
      if (text.indexOf("{{") !== -1 || text.indexOf("{%") !== -1) { return; }
      code.setAttribute("data-highlighted", "1");
      code.innerHTML = looksLikeLua(text) ? highlightLua(text) : highlightJson(text);
    });
  }

  function restoreScroll(element, key) {
    var saved = sessionStorage.getItem(key);
    if (saved !== null) { element.scrollTop = parseInt(saved, 10) || 0; }
  }

  function rememberScroll(element, key) {
    restoreScroll(element, key);
    element.addEventListener("scroll", function () {
      sessionStorage.setItem(key, String(element.scrollTop));
    }, { passive: true });
  }

  function restoreNavigation() {
    var sidebar = document.querySelector(".docs-sidebar");
    if (!sidebar) { return; }

    var panel = sidebar.querySelector(".docs-nav-panel");
    var mobileNav = sidebar.querySelector(".docs-nav-panel > nav");
    if (panel) {
      var mobile = window.matchMedia("(max-width: 1000px)");
      function applyPanelState() {
        panel.open = !mobile.matches || sessionStorage.getItem("docs-nav-open") === "1";
      }
      applyPanelState();
      mobile.addEventListener("change", applyPanelState);
      panel.querySelector("summary").addEventListener("click", function () {
        if (mobile.matches) {
          sessionStorage.setItem("docs-nav-open", panel.open ? "0" : "1");
        }
      });
      panel.addEventListener("toggle", function () {
        if (panel.open && mobileNav) {
          restoreScroll(mobileNav, "docs-mobile-nav-scroll");
        }
      });
    }

    rememberScroll(sidebar, "docs-sidebar-scroll");
    if (mobileNav) { rememberScroll(mobileNav, "docs-mobile-nav-scroll"); }
  }

  function init() {
    highlightAll();
    restoreNavigation();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
