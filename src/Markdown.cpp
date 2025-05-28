#include "Markdown.h"
#include <utility>

namespace markdown {

std::vector<std::unique_ptr<BlockNode>> parse(const BString &text) {
  return std::vector<std::unique_ptr<BlockNode>>();
}

bool BlockNode::operator==(const BlockNode &other) const {
  return typeid(*this) == typeid(other);
}

bool SpanNode::operator==(const SpanNode &other) const {
  return typeid(*this) == typeid(other);
}

ParagraphNode::ParagraphNode(std::vector<std::unique_ptr<SpanNode>> contents)
    :
    contents(std::move(contents)) {}

ParagraphNode::ParagraphNode(
    std::initializer_list<std::unique_ptr<SpanNode>> init)
    :
    contents(std::move(contents)) {}

bool ParagraphNode::operator==(const BlockNode &other) const {
  if (!BlockNode::operator==(other))
    return false;
  const auto &o = static_cast<const ParagraphNode &>(other);
  if (this->contents.size() != o.contents.size())
    return false;
  auto c1 = this->contents.begin();
  auto c2 = o.contents.begin();
  for (; c1 != this->contents.end() && c2 != this->contents.end(); c1++, c2++) {
    if (**c1 != **c2)
      return false;
  }
  return true;
}

TextNode::TextNode(const BString &contents)
    :
    contents(contents) {}

bool TextNode::operator==(const SpanNode &other) const {
  if (!SpanNode::operator==(other))
    return false;
  const auto &o = static_cast<const TextNode &>(other);
  return this->contents == o.contents;
}
} // namespace markdown
