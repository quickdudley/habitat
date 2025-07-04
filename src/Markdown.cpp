#include "Markdown.h"
#include <StringList.h>
#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
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

float ParagraphNode::heightForWidth(float width) { return 1; }

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

const BString &TextNode::getText() const { return this->contents; }

std::vector<std::pair<BString, bool>> TextNode::getTokens() const {
  std::vector<std::pair<BString, bool>> tokens;
  if (this->contents.Length() == 0)
    return tokens;
  U_ICU_NAMESPACE::UnicodeString ustr =
      U_ICU_NAMESPACE::UnicodeString::fromUTF8(this->contents.String());
  UErrorCode status = U_ZERO_ERROR;
  // TODO: find out whether or not the Haiku locale kit can inform
  // `icu::Locale` initialization
  std::unique_ptr<U_ICU_NAMESPACE::BreakIterator> lineBreak(
      U_ICU_NAMESPACE::BreakIterator::createLineInstance(
          U_ICU_NAMESPACE::Locale::getDefault(), status));
  lineBreak->setText(ustr);
  int32 start = lineBreak->first();
  int32 end = lineBreak->next();
  while (end != icu::BreakIterator::DONE) {
    U_ICU_NAMESPACE::UnicodeString segment;
    ustr.extractBetween(start, end, segment);
    bool wsToken = true;
    for (int32 i = 0; i < segment.length(); i++) {
      if (!u_isUWhiteSpace(segment.char32At(i))) {
        wsToken = false;
        break;
      }
    }
    std::string packed;
    segment.toUTF8String(packed);
    tokens.push_back({BString(packed.c_str()), wsToken});
    start = end;
    end = lineBreak->next();
  }
  return tokens;
}

void TextNode::measureToken(const BString &token, float &width,
                            float &height) const {
  // TODO: implement this
}
} // namespace markdown
