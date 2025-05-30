#include "BJSON.h"
#include "JSON.h"
#include "sample576.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Handles unicode", "[JSON]") {
  BString source("🀐");
  BString result = JSON::escapeString(source);
  REQUIRE(result == "\"🀐\"");
}

TEST_CASE("Escapes overwide UTF-8 NUL", "[JSON]") {
  unsigned char pseudoNull[] = {0xC0, 0x80, 0};
  BString source((char *)pseudoNull);
  BString result = JSON::escapeString(source);
  REQUIRE(result == "\"\\u0000\"");
}

TEST_CASE("Correctly parses multiple of 10", "[JSON][parsing][number]") {
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(double *target)
        :
        target(target) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      return std::make_unique<AExpect>(this->target);
    }

  private:
    double *target;
  };
  char example[] = "[1491901740000]";
  double result;
  REQUIRE(JSON::parse(std::make_unique<AExpect>(&result), example) == B_OK);
  REQUIRE(result == 1491901740000.0);
}

TEST_CASE("Correctly parses decimal with trailing 0",
          "[JSON][parsing][number]") {
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(double *target)
        :
        target(target) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      return std::unique_ptr<NodeSink>(std::make_unique<AExpect>(this->target));
    }

  private:
    double *target;
  };
  char example[] = "[10.00]";
  double result;
  REQUIRE(JSON::parse(std::make_unique<AExpect>(&result), example) == B_OK);
  REQUIRE(result == 10.00);
}

TEST_CASE("Correctly parses negative number", "[JSON][parsing][number]") {
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(double *target)
        :
        target(target) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      return std::unique_ptr<NodeSink>(std::make_unique<AExpect>(this->target));
    }

  private:
    double *target;
  };
  char example[] = "[-10.00]";
  double result;
  REQUIRE(JSON::parse(std::make_unique<AExpect>(&result), example) == B_OK);
  REQUIRE(result == -10.00);
}

TEST_CASE("Correctly parses suspicious number", "[JSON][parsing][number]") {
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(double *target)
        :
        target(target) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      return std::unique_ptr<NodeSink>(std::make_unique<AExpect>(this->target));
    }

  private:
    double *target;
  };
  char example[] = "[297.09000000000003]";
  double result;
  REQUIRE(JSON::parse(std::make_unique<AExpect>(&result), example) == B_OK);
  REQUIRE(result == 297.09000000000003);
}

TEST_CASE("Successfully parses object with empty array as properties",
          "[JSON][parsing]") {
  char example[] = "{\"a\": [], \"b\": []}";
  JSON::Parser parser(std::make_unique<JSON::IgnoreNode>());
  for (int i = 0; example[i] != 0; i++)
    REQUIRE(parser.nextChar(example[i]) == B_OK);
}

TEST_CASE("Correctly parses objects in array document", "[JSON][parsing]") {
  struct Results {
    bool a1 = false, b1 = false, c1 = false, d1 = false, e1 = false, f1 = false;
    bool a2 = false, b2 = false, c2 = false, d2 = false, e2 = false, f2 = false;
    bool extra = false;
  };
  class OExpect : public JSON::NodeSink {
  public:
    OExpect(Results *target, int ix)
        :
        target(target),
        ix(ix) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      this->target->extra = true;
    }
    void addBool(const BString &rawname, const BString &name,
                 bool value) override {
      this->target->extra = true;
    }
    void addNull(const BString &rawname, const BString &name) override {
      this->target->extra = true;
    }
    void addString(const BString &rawname, const BString &name,
                   const BString &raw, const BString &value) override {
      if (name == "a") {
        if (this->ix == 0)
          this->target->a1 = true;
        else
          this->target->a2 = true;
        if (value == "b") {
          if (this->ix == 0)
            this->target->b1 = true;
          else
            this->target->b2 = true;
        }
      } else if (name == "c") {
        if (this->ix == 0)
          this->target->c1 = true;
        else
          this->target->c2 = true;
        if (value == "d") {
          if (this->ix == 0)
            this->target->d1 = true;
          else
            this->target->d2 = true;
        }
      } else if (name == "e") {
        if (this->ix == 0)
          this->target->e1 = true;
        else
          this->target->e2 = true;
        if (value == "f") {
          if (this->ix == 0)
            this->target->f1 = true;
          else
            this->target->f2 = true;
        }
      } else {
        this->target->extra = true;
      }
    }
    std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                        const BString &name) override {
      this->target->extra = true;
      return JSON::NodeSink::addObject(rawname, name);
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      this->target->extra = true;
      return JSON::NodeSink::addArray(rawname, name);
    }

  private:
    Results *target;
    int ix;
  };
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(Results *target)
        :
        target(target),
        ix(0) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      this->target->extra = true;
    }
    void addBool(const BString &rawname, const BString &name,
                 bool value) override {
      this->target->extra = true;
    }
    void addNull(const BString &rawname, const BString &name) override {
      this->target->extra = true;
    }
    void addString(const BString &rawname, const BString &name,
                   const BString &raw, const BString &value) override {
      this->target->extra = true;
    }
    std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                        const BString &name) override {
      return std::make_unique<OExpect>(this->target, this->ix++);
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      this->target->extra = true;
      return JSON::NodeSink::addArray(rawname, name);
    }

  private:
    Results *target;
    int ix;
  };
  class RootExpect : public JSON::NodeSink {
  public:
    RootExpect(Results *target)
        :
        target(target) {}
    void addNumber(const BString &rawname, const BString &name,
                   const BString &raw, JSON::number value) override {
      this->target->extra = true;
    }
    void addBool(const BString &rawname, const BString &name,
                 bool value) override {
      this->target->extra = true;
    }
    void addNull(const BString &rawname, const BString &name) override {
      this->target->extra = true;
    }
    void addString(const BString &rawname, const BString &name,
                   const BString &raw, const BString &value) override {
      this->target->extra = true;
    }
    std::unique_ptr<NodeSink> addObject(const BString &rawname,
                                        const BString &name) override {
      this->target->extra = true;
      return JSON::NodeSink::addObject(rawname, name);
    }
    std::unique_ptr<NodeSink> addArray(const BString &rawname,
                                       const BString &name) override {
      return std::make_unique<AExpect>(this->target);
    }

  private:
    Results *target;
  };

  Results results;
  char example[] = "[{\"a\":\"b\",\"c\":\"d\"},{\"e\":\"f\"}]";
  {
    JSON::Parser parser(std::make_unique<RootExpect>(&results));
    for (int i = 0; i < sizeof(example); i++)
      parser.nextChar(example[i]);
  }
  REQUIRE(results.a1 == true);
  REQUIRE(results.b1 == true);
  REQUIRE(results.c1 == true);
  REQUIRE(results.d1 == true);
  REQUIRE(results.e1 == false);
  REQUIRE(results.f1 == false);
  REQUIRE(results.a2 == false);
  REQUIRE(results.b2 == false);
  REQUIRE(results.c2 == false);
  REQUIRE(results.d2 == false);
  REQUIRE(results.e2 == true);
  REQUIRE(results.f2 == true);
  REQUIRE(results.extra == false);
}

TEST_CASE("Correctly serialises numbers", "[JSON]") {
  BString target;
#define EX(n)                                                                  \
  target = "";                                                                 \
  {                                                                            \
    JSON::RootSink rootSink(std::make_unique<JSON::SerializerStart>(&target)); \
    BString blank;                                                             \
    rootSink.addNumber(blank, n);                                              \
  }                                                                            \
  REQUIRE(target == #n)
  EX(0);
  EX(1);
  EX(0.2);
  EX(0.7);
  EX(-10);
  EX(1613858272380.0059);
  EX(-0.216097777777);
  EX(297.09000000000003);
  EX(1476052034270);
  EX(1493771736542);
  EX(45.448031);
  EX(-122.6440624);
  EX(21.320999145507812);
#undef EX
}

TEST_CASE("Correctly restores previosuly failing number on round-trip",
          "[JSON]") {
  char original[] = "[\n  1613858272380.0059\n]";
  BString reconstructed;
  {
    BMessage msg;
    JSON::parse(std::make_unique<JSON::BMessageDocSink>(&msg), original);
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&reconstructed));
    JSON::fromBMessage(&rootSink, &msg);
  }
  REQUIRE(original == reconstructed);
}

TEST_CASE("Correctly parses previously failing message 576 from CEL",
          "[JSON]") {
  BString sample(sample576, sample576_len);
  BMessage parsed;
  JSON::parse(std::make_unique<JSON::BMessageDocSink>(&parsed), sample);
  BString reconstructed;
  {
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&reconstructed));
    JSON::fromBMessage(&rootSink, &parsed);
  }
  REQUIRE(sample == reconstructed);
}

TEST_CASE("Round-trips escape sequences found in failing message", "[JSON]") {
  char original[] = "[\n  \"\\\"\\f\\u000b\"\n]";
  BMessage parsed;
  JSON::parse(std::make_unique<JSON::BMessageDocSink>(&parsed), original);
  BString reconstructed;
  {
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&reconstructed));
    JSON::fromBMessage(&rootSink, &parsed);
  }
  REQUIRE(reconstructed == original);
}

TEST_CASE("Generates JSON without additional whitespace when required",
          "[JSON]") {
  BMessage outer('JSOB');
  BMessage a('JSAR');
  a.AddDouble("0", 1.0);
  a.AddBool("1", false);
  outer.AddMessage("a", &a);
  BMessage b('JSOB');
  b.AddDouble("c", 2.0);
  outer.AddMessage("b", &b);
  BString output;
  {
    JSON::RootSink rootSink(
        std::make_unique<JSON::SerializerStart>(&output, 0, false));
    JSON::fromBMessage(&rootSink, &outer);
  }
  REQUIRE(output == "{\"a\":[1,false],\"b\":{\"c\":2}}");
}

TEST_CASE("Generates JSON with additional whitespace by default", "[JSON]") {
  BMessage outer('JSOB');
  BMessage a('JSAR');
  a.AddDouble("0", 1.0);
  a.AddBool("1", false);
  outer.AddMessage("a", &a);
  BMessage b('JSOB');
  b.AddDouble("c", 2.0);
  outer.AddMessage("b", &b);
  BString output;
  {
    JSON::RootSink rootSink(std::make_unique<JSON::SerializerStart>(&output));
    JSON::fromBMessage(&rootSink, &outer);
  }
  REQUIRE(output ==
          "{\n"
          "  \"a\": [\n"
          "    1,\n"
          "    false\n"
          "  ],\n"
          "  \"b\": {\n"
          "    \"c\": 2\n"
          "  }\n"
          "}");
}
