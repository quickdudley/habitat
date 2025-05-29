#include "Markdown.h"
#include <StringList.h>
#include <utility>

namespace markdown {
namespace {
class ParseContext {
public:
  ParseContext(std::vector<std::unique_ptr<BlockNode>> *sink);
  std::vector<std::unique_ptr<BlockNode>> *sink;
};

ParseContext::ParseContext(std::vector<std::unique_ptr<BlockNode>> *sink)
    :
    sink(sink) {}

bool parseLine(const BString &line, void *arg) {
  auto ctx = (ParseContext *)arg;
  std::vector<std::unique_ptr<SpanNode>> spanNodes;
  spanNodes.push_back(std::make_unique<TextNode>(line));
  ctx->sink->push_back(std::make_unique<ParagraphNode>(std::move(spanNodes)));
  return false;
}
} // namespace

std::vector<std::unique_ptr<BlockNode>> parse(const BString &text) {
  std::vector<std::unique_ptr<BlockNode>> result;
  BStringList lines;
  text.Split("\n", true, lines);
  ParseContext ctx(&result);
  lines.DoForEach(parseLine, (void *)&ctx);
  return result;
}

bool BlockNode::operator==(const BlockNode &other) const {
  return typeid(*this) == typeid(other);
}

bool SpanNode::operator==(const SpanNode &other) const {
  return typeid(*this) == typeid(other);
}

std::ostream &operator<<(std::ostream &os, BlockNode const &value) {
  return os << value.toString();
}

std::ostream &operator<<(std::ostream &os, SpanNode const &value) {
  return os << value.toString();
}

BString BlockNode::toString() const { return "BlockNode"; }

BString SpanNode::toString() const { return "SpanNode"; }

ParagraphNode::ParagraphNode(std::vector<std::unique_ptr<SpanNode>> contents)
    :
    contents(std::move(contents)) {}

BString ParagraphNode::toString() const {
  BString result("ParagraphNode {");
  BString delimiter(" ");
  for (auto &span : this->contents) {
    result << delimiter;
    result << span->toString();
    delimiter = ", ";
  }
  result << " }";
  return result;
}

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

BString TextNode::toString() const {
  BString result("TextNode \"");
  result << this->contents;
  result << "\"";
  return result;
}
} // namespace markdown
