#ifndef MARKDOWN_H
#define MARKDOWN_H
#include <Font.h>
#include <String.h>
#include <View.h>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

namespace markdown {
class BlockNode {
public:
  virtual ~BlockNode() {}
  virtual bool operator==(const BlockNode &other) const;
  bool operator!=(const BlockNode &other) const { return !(*this == other); }
  virtual BString toString() const;
  virtual float heightForWidth(float width) = 0;
  virtual void draw(BView *view, BRect &frame) const = 0;
};

std::vector<std::unique_ptr<BlockNode>> parse(const BString &text);
std::ostream &operator<<(std::ostream &os, BlockNode const &value);

struct SpanMetrics {
  float width;
  float ascent;
  float descent;
  float leading;
};

class SpanNode {
public:
  virtual ~SpanNode() {}
  virtual bool operator==(const SpanNode &other) const;
  bool operator!=(const SpanNode &other) const { return !(*this == other); }
  virtual BString toString() const;
  virtual BFont getFont() const = 0;
  virtual const BString &getText() const = 0;
  virtual std::vector<std::pair<BString, bool>> getTokens() const = 0;
  virtual SpanMetrics measureToken(const BString &token) const = 0;
  virtual float drawToken(const BString &token, BView *view) const = 0;
};
std::ostream &operator<<(std::ostream &os, SpanNode const &value);

class ParagraphNode : public BlockNode {
public:
  ParagraphNode(std::vector<std::unique_ptr<SpanNode>> contents);
  bool operator==(const BlockNode &other) const override;
  BString toString() const override;
  float heightForWidth(float width);
  void draw(BView *view, BRect &frame) const override;

private:
  std::vector<std::unique_ptr<SpanNode>> contents;
};

class TextNode : public SpanNode {
public:
  TextNode(const BString &contents);
  bool operator==(const SpanNode &other) const override;
  BString toString() const override;
  BFont getFont() const override;
  const BString &getText() const override;
  std::vector<std::pair<BString, bool>> getTokens() const override;
  SpanMetrics measureToken(const BString &token) const override;
  float drawToken(const BString &token, BView *view) const;

private:
  BString contents;
};
} // namespace markdown

#endif
