#include "JSON.h"
#include "Markdown.h"
#include "markdown_spec.h"
#include <catch2/catch_all.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <map>
#include <utility>

static std::map<JSON::number, BString> *testcases = NULL;

TEST_CASE("Example 219", "[markdown]") {
  auto parsed = markdown::parse((*testcases)[219.0]);
  REQUIRE(parsed.size() == 2);
  REQUIRE(*parsed[0] ==
          markdown::ParagraphNode
              {std::make_unique<markdown::TextNode>("aaa")});
  REQUIRE(*parsed[1] ==
          markdown::ParagraphNode
              {std::make_unique<markdown::TextNode>("bbb")});
}

namespace {
class TestCasesSink : public JSON::NodeSink {
public:
  std::unique_ptr<JSON::NodeSink> addObject(const BString &rawname,
                                            const BString &name) override;
  std::unique_ptr<JSON::NodeSink> addArray(const BString &rawname,
                                           const BString &name) override;
};
} // namespace

class ParseTestCases : public Catch::EventListenerBase {
public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(Catch::TestRunInfo const &) override {
    testcases = new std::map<JSON::number, BString>();
    JSON::Parser parser(std::make_unique<TestCasesSink>());
    for (unsigned int i = 0; i < generated_test_markdown_spec_json_len; i++)
      parser.nextChar(generated_test_markdown_spec_json[i]);
  }
};

CATCH_REGISTER_LISTENER(ParseTestCases)

namespace {
class OneCase : public JSON::NodeSink {
public:
  ~OneCase();
  void addString(const BString &rawname, const BString &name,
                 const BString &raw, const BString &value) override;
  void addNumber(const BString &rawname, const BString &name,
                 const BString &raw, JSON::number value) override;

private:
  BString markdown;
  JSON::number example;
};

OneCase::~OneCase() {
  if (this->markdown != "" && example > 0)
    testcases->insert({this->example, this->markdown});
}

void OneCase::addString(const BString &rawname, const BString &name,
                        const BString &raw, const BString &value) {
  if (name == "markdown")
    this->markdown = value;
}

void OneCase::addNumber(const BString &rawname, const BString &name,
                        const BString &raw, JSON::number value) {
  if (name == "example")
    this->example = value;
}

std::unique_ptr<JSON::NodeSink> TestCasesSink::addArray(const BString &rawname,
                                                        const BString &name) {
  return std::make_unique<TestCasesSink>();
}

std::unique_ptr<JSON::NodeSink> TestCasesSink::addObject(const BString &rawname,
                                                         const BString &name) {
  return std::make_unique<OneCase>();
}
} // namespace
