#include "editor/EditorAuthoredJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <ranges>
#include <vector>

namespace demi::editor {
namespace {

struct SourceNode {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t entryBegin = 0;
  std::map<std::string, SourceNode> members;
  std::vector<SourceNode> elements;
};

void skipSpace(const std::string &text, std::size_t &cursor) {
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])))
    ++cursor;
}

bool stringEnd(const std::string &text, std::size_t &cursor) {
  if (cursor >= text.size() || text[cursor] != '"')
    return false;
  for (++cursor; cursor < text.size(); ++cursor) {
    if (text[cursor] == '\\') {
      ++cursor;
      continue;
    }
    if (text[cursor] == '"') {
      ++cursor;
      return true;
    }
  }
  return false;
}

bool parseNode(const std::string &text, std::size_t &cursor, SourceNode &node) {
  skipSpace(text, cursor);
  node.begin = cursor;
  node.entryBegin = cursor;
  if (cursor >= text.size())
    return false;
  if (text[cursor] == '{') {
    ++cursor;
    skipSpace(text, cursor);
    while (cursor < text.size() && text[cursor] != '}') {
      const std::size_t keyBegin = cursor;
      if (!stringEnd(text, cursor))
        return false;
      const std::string keyText = text.substr(keyBegin, cursor - keyBegin);
      const std::string key = nlohmann::json::parse(keyText).get<std::string>();
      skipSpace(text, cursor);
      if (cursor >= text.size() || text[cursor++] != ':')
        return false;
      SourceNode child;
      if (!parseNode(text, cursor, child))
        return false;
      child.entryBegin = keyBegin;
      node.members.emplace(key, std::move(child));
      skipSpace(text, cursor);
      if (cursor < text.size() && text[cursor] == ',') {
        ++cursor;
        skipSpace(text, cursor);
      } else {
        break;
      }
    }
    if (cursor >= text.size() || text[cursor++] != '}')
      return false;
  } else if (text[cursor] == '[') {
    ++cursor;
    skipSpace(text, cursor);
    while (cursor < text.size() && text[cursor] != ']') {
      SourceNode child;
      if (!parseNode(text, cursor, child))
        return false;
      node.elements.push_back(std::move(child));
      skipSpace(text, cursor);
      if (cursor < text.size() && text[cursor] == ',') {
        ++cursor;
        skipSpace(text, cursor);
      } else {
        break;
      }
    }
    if (cursor >= text.size() || text[cursor++] != ']')
      return false;
  } else if (text[cursor] == '"') {
    if (!stringEnd(text, cursor))
      return false;
  } else {
    while (cursor < text.size() && text[cursor] != ',' && text[cursor] != '}' &&
           text[cursor] != ']' &&
           !std::isspace(static_cast<unsigned char>(text[cursor])))
      ++cursor;
  }
  node.end = cursor;
  return node.end > node.begin;
}

std::string decodePointerToken(std::string token) {
  for (std::size_t cursor = 0; cursor + 1 < token.size();) {
    if (token[cursor] == '~' && token[cursor + 1] == '1')
      token.replace(cursor, 2, "/");
    else if (token[cursor] == '~' && token[cursor + 1] == '0')
      token.replace(cursor, 2, "~");
    else
      ++cursor;
  }
  return token;
}

const SourceNode *findNode(const SourceNode &root, const std::string &pointer) {
  const SourceNode *node = &root;
  for (std::size_t begin = 1; begin <= pointer.size();) {
    const std::size_t end = pointer.find('/', begin);
    const std::string token = decodePointerToken(
        pointer.substr(begin, end == std::string::npos ? pointer.size() - begin
                                                       : end - begin));
    if (!node->members.empty()) {
      const auto found = node->members.find(token);
      if (found == node->members.end())
        return nullptr;
      node = &found->second;
    } else if (!node->elements.empty()) {
      try {
        const std::size_t index = std::stoul(token);
        if (index >= node->elements.size())
          return nullptr;
        node = &node->elements[index];
      } catch (const std::exception &) {
        return nullptr;
      }
    } else {
      return nullptr;
    }
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return node;
}

const SourceNode *findMemberExample(const SourceNode &node,
                                    const std::string &member,
                                    const SourceNode *excludedParent) {
  if (&node != excludedParent) {
    const auto found = node.members.find(member);
    if (found != node.members.end())
      return &found->second;
  }
  for (const auto &[key, child] : node.members) {
    static_cast<void>(key);
    if (const SourceNode *found =
            findMemberExample(child, member, excludedParent))
      return found;
  }
  for (const SourceNode &child : node.elements)
    if (const SourceNode *found =
            findMemberExample(child, member, excludedParent))
      return found;
  return nullptr;
}

std::string replacementText(const std::string &original,
                            const nlohmann::json &value) {
  if (value.is_array() && original.find('\n') == std::string::npos) {
    const bool spaced = original.find(", ") != std::string::npos;
    std::string result = "[";
    for (std::size_t index = 0; index < value.size(); ++index) {
      if (index > 0)
        result += spaced ? ", " : ",";
      result += value[index].dump();
    }
    return result + ']';
  }
  return value.dump();
}

std::pair<std::string, std::string> splitPointer(const std::string &pointer) {
  const std::size_t separator = pointer.find_last_of('/');
  return {separator == 0 ? std::string{} : pointer.substr(0, separator),
          decodePointerToken(pointer.substr(separator + 1))};
}

std::string indentationAt(const std::string &text, const std::size_t position) {
  const std::size_t line = text.rfind('\n', position);
  const std::size_t begin = line == std::string::npos ? 0 : line + 1;
  std::size_t cursor = begin;
  while (cursor < position && (text[cursor] == ' ' || text[cursor] == '\t'))
    ++cursor;
  return text.substr(begin, cursor - begin);
}

std::string indentMultiline(std::string value, const std::string &indent) {
  for (std::size_t newline = value.find('\n'); newline != std::string::npos;
       newline = value.find('\n', newline + indent.size() + 1))
    value.insert(newline + 1, indent);
  return value;
}

std::string formatValueLike(const nlohmann::json &value,
                            const SourceNode *example,
                            const std::string &source,
                            const std::string &indent) {
  if (example == nullptr)
    return indentMultiline(value.dump(2), indent);
  const bool multiline = source.find('\n', example->begin) < example->end;
  if (value.is_object() && !example->members.empty()) {
    if (value.empty())
      return "{}";
    std::vector<std::string> keys;
    keys.reserve(value.size());
    std::vector<std::pair<std::size_t, std::string>> existing;
    for (const auto &[key, node] : example->members)
      if (value.contains(key))
        existing.emplace_back(node.entryBegin, key);
    std::ranges::sort(existing);
    for (const auto &[position, key] : existing) {
      static_cast<void>(position);
      keys.push_back(key);
    }
    for (const auto &[key, child] : value.items()) {
      static_cast<void>(child);
      if (std::ranges::find(keys, key) == keys.end())
        keys.push_back(key);
    }
    const std::string childIndent = indent + "  ";
    std::string result = "{";
    for (std::size_t index = 0; index < keys.size(); ++index) {
      const auto found = example->members.find(keys[index]);
      const SourceNode *childExample =
          found == example->members.end() ? nullptr : &found->second;
      result += (index == 0 ? (multiline ? "\n" + childIndent : "")
                            : (multiline ? ",\n" + childIndent : ", "));
      result += nlohmann::json(keys[index]).dump() + ": " +
                formatValueLike(value.at(keys[index]), childExample, source,
                                childIndent);
    }
    return result + (multiline ? "\n" + indent + "}" : "}");
  }
  if (value.is_array() && !example->elements.empty()) {
    if (!multiline)
      return replacementText(
          source.substr(example->begin, example->end - example->begin), value);
    const std::string childIndent = indent + "  ";
    std::string result = "[";
    for (std::size_t index = 0; index < value.size(); ++index) {
      const SourceNode *childExample = index < example->elements.size()
                                           ? &example->elements[index]
                                           : &example->elements.back();
      result +=
          (index == 0 ? "\n" + childIndent : ",\n" + childIndent) +
          formatValueLike(value[index], childExample, source, childIndent);
    }
    return result + "\n" + indent + "]";
  }
  return value.dump();
}

std::string newValueText(const nlohmann::json &value, const bool multiline,
                         const std::string &indent, const SourceNode *example,
                         const std::string &source) {
  return example != nullptr ? formatValueLike(value, example, source, indent)
         : multiline        ? indentMultiline(value.dump(2), indent)
                            : value.dump();
}

bool addAtPointer(std::string &text, const SourceNode &root,
                  const std::string &pointer, const nlohmann::json &value) {
  const auto [parentPointer, token] = splitPointer(pointer);
  const SourceNode *parent = findNode(root, parentPointer);
  if (parent == nullptr || parent->end <= parent->begin + 1)
    return false;
  bool multiline = text.find('\n', parent->begin) < parent->end;
  const std::string parentIndent = indentationAt(text, parent->begin);
  std::string childIndent = parentIndent + "  ";
  if (!parent->elements.empty())
    childIndent = indentationAt(text, parent->elements.front().entryBegin);
  else if (!parent->members.empty())
    childIndent =
        indentationAt(text, parent->members.begin()->second.entryBegin);

  if (text[parent->begin] == '[') {
    std::size_t index = parent->elements.size();
    if (token != "-") {
      try {
        index = std::stoul(token);
      } catch (const std::exception &) {
        return false;
      }
    }
    const SourceNode *example = parent->elements.empty() ? nullptr
                                : index < parent->elements.size()
                                    ? &parent->elements[index]
                                    : &parent->elements.back();
    const std::string serialized =
        newValueText(value, multiline, childIndent, example, text);
    if (parent->elements.empty()) {
      const std::string insertion =
          multiline ? "\n" + childIndent + serialized + "\n" + parentIndent
                    : serialized;
      text.insert(parent->end - 1, insertion);
    } else if (index >= parent->elements.size()) {
      const SourceNode &last = parent->elements.back();
      text.insert(last.end, multiline ? ",\n" + childIndent + serialized
                                      : ", " + serialized);
    } else {
      const SourceNode &next = parent->elements[index];
      text.insert(next.begin, multiline ? serialized + ",\n" + childIndent
                                        : serialized + ", ");
    }
    return true;
  }
  if (text[parent->begin] != '{')
    return false;
  const SourceNode *example =
      findMemberExample(root, token, parent);
  if (example != nullptr &&
      text.find('\n', example->entryBegin) < example->end)
    multiline = true;
  const std::string member =
      nlohmann::json(token).dump() + ": " +
      newValueText(value, multiline, childIndent, example, text);
  if (parent->members.empty()) {
    text.insert(parent->end - 1,
                multiline ? "\n" + childIndent + member + "\n" + parentIndent
                          : member);
  } else {
    const auto last = std::ranges::max_element(
        parent->members, {}, [](const auto &item) { return item.second.end; });
    text.insert(last->second.end,
                multiline ? ",\n" + childIndent + member : ", " + member);
  }
  return true;
}

bool removeAtPointer(std::string &text, const SourceNode &root,
                     const std::string &pointer) {
  const SourceNode *node = findNode(root, pointer);
  if (node == nullptr || node == &root)
    return false;
  std::size_t begin = node->entryBegin;
  std::size_t end = node->end;
  std::size_t cursor = end;
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])))
    ++cursor;
  if (cursor < text.size() && text[cursor] == ',') {
    ++cursor;
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])))
      ++cursor;
    end = cursor;
  } else {
    cursor = begin;
    while (cursor > 0 &&
           std::isspace(static_cast<unsigned char>(text[cursor - 1])))
      --cursor;
    if (cursor > 0 && text[cursor - 1] == ',')
      begin = cursor - 1;
  }
  text.erase(begin, end - begin);
  return true;
}

void normalize(nlohmann::json &value, const nlohmann::json *previous,
               const double factor) {
  if (value.is_number_float()) {
    const double rounded = std::round(value.get<double>() * factor) / factor;
    if (previous != nullptr && previous->is_number() &&
        previous->get<double>() == rounded)
      value = *previous;
    else
      value = rounded;
    return;
  }
  if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index) {
      const nlohmann::json *old = previous != nullptr && previous->is_array() &&
                                          index < previous->size()
                                      ? &(*previous)[index]
                                      : nullptr;
      normalize(value[index], old, factor);
    }
  } else if (value.is_object()) {
    for (auto &[key, child] : value.items()) {
      const nlohmann::json *old = previous != nullptr &&
                                          previous->is_object() &&
                                          previous->contains(key)
                                      ? &previous->at(key)
                                      : nullptr;
      normalize(child, old, factor);
    }
  }
}

} // namespace

nlohmann::json normalizeEditorAuthoredValue(nlohmann::json value,
                                            const nlohmann::json *previous,
                                            const int decimalPlaces) {
  normalize(value, previous, std::pow(10.0, std::max(decimalPlaces, 0)));
  return value;
}

std::optional<std::string> patchEditorJsonSource(const std::string &source,
                                                 const nlohmann::json &before,
                                                 const nlohmann::json &after) {
  const nlohmann::json changes = nlohmann::json::diff(before, after);
  if (changes.empty())
    return source;
  std::string result = source;
  nlohmann::json current = before;
  for (const nlohmann::json &change : changes) {
    std::size_t cursor = 0;
    SourceNode root;
    if (!parseNode(result, cursor, root))
      return std::nullopt;
    const std::string operation = change.value("op", "");
    const std::string pointer = change.value("path", "");
    bool applied = false;
    if (operation == "replace" && change.contains("value")) {
      const SourceNode *node = findNode(root, pointer);
      if (node != nullptr) {
        result.replace(
            node->begin, node->end - node->begin,
            replacementText(result.substr(node->begin, node->end - node->begin),
                            change["value"]));
        applied = true;
      }
    } else if (operation == "add" && change.contains("value")) {
      applied = addAtPointer(result, root, pointer, change["value"]);
    } else if (operation == "remove") {
      applied = removeAtPointer(result, root, pointer);
    }
    if (!applied)
      return std::nullopt;
    current = current.patch(nlohmann::json::array({change}));
  }
  return current == after ? std::make_optional(std::move(result))
                          : std::nullopt;
}

} // namespace demi::editor
