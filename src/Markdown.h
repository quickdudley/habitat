#ifndef MARKDOWN_H
#define MARKDOWN_H
#include <String.h>
#include <memory>
#include <ostream>
#include <vector>

namespace markdown {
class BlockNode {
public:
  virtual ~BlockNode() {}
  virtual bool operator==(const BlockNode &other) const;
  bool operator!=(const BlockNode &other) const { return !(*this == other); }
  virtual BString toString() const;
};

std::vector<std::unique_ptr<BlockNode>> parse(const BString &text);
std::ostream &operator<<(std::ostream &os, BlockNode const &value);

class SpanNode {
public:
  virtual ~SpanNode() {}
  virtual bool operator==(const SpanNode &other) const;
  bool operator!=(const SpanNode &other) const { return !(*this == other); }
  virtual BString toString() const;
};
std::ostream &operator<<(std::ostream &os, SpanNode const &value);

class ParagraphNode : public BlockNode {
public:
  ParagraphNode(std::vector<std::unique_ptr<SpanNode>> contents);
  bool operator==(const BlockNode &other) const override;
  BString toString() const override;

private:
  std::vector<std::unique_ptr<SpanNode>> contents;
};

class TextNode : public SpanNode {
public:
  TextNode(const BString &contents);
  bool operator==(const SpanNode &other) const override;
  BString toString() const override;

private:
  BString contents;
};
} // namespace markdown

#endif
