#include "JSON.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Handles unicode", "[JSON]") {
  BString source("🀐");
  BString result = JSON::escapeString(source);
  REQUIRE(result == "\"🀐\"");
}

TEST_CASE("Correctly parses multiple of 10", "[JSON][parsing][number]") {
  class AExpect : public JSON::NodeSink {
  public:
    AExpect(double *target)
        :
        target(target) {}
    void addNumber(BString &rawname, BString &name, BString &raw,
                   JSON::number value) {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name) {
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
    void addNumber(BString &rawname, BString &name, BString &raw,
                   JSON::number value) {
      *(this->target) = value;
    }
    std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name) {
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

TEST_CASE("Successfully parses object with empty array as properties",
          "[JSON][parsing]") {
  char example[] = "{\"a\": [], \"b\": []}";
  JSON::Parser parser(std::make_unique<JSON::IgnoreNode>());
  for (int i = 0; example[i] != 0; i++) {
    REQUIRE(parser.nextChar(example[i]) == B_OK);
  }
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
    void addNumber(BString &rawname, BString &name, BString &raw,
                   JSON::number value) {
      this->target->extra = true;
    }
    void addBool(BString &rawname, BString &name, bool value) {
      this->target->extra = true;
    }
    void addNull(BString &rawname, BString &name) {
      this->target->extra = true;
    }
    void addString(BString &rawname, BString &name, BString &raw,
                   BString &value) {
      if (name == "a") {
        if (this->ix == 0) {
          this->target->a1 = true;
        } else {
          this->target->a2 = true;
        }
        if (value == "b") {
          if (this->ix == 0) {
            this->target->b1 = true;
          } else {
            this->target->b2 = true;
          }
        }
      } else if (name == "c") {
        if (this->ix == 0) {
          this->target->c1 = true;
        } else {
          this->target->c2 = true;
        }
        if (value == "d") {
          if (this->ix == 0) {
            this->target->d1 = true;
          } else {
            this->target->d2 = true;
          }
        }
      } else if (name == "e") {
        if (this->ix == 0) {
          this->target->e1 = true;
        } else {
          this->target->e2 = true;
        }
        if (value == "f") {
          if (this->ix == 0) {
            this->target->f1 = true;
          } else {
            this->target->f2 = true;
          }
        }
      } else {
        this->target->extra = true;
      }
    }
    std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name) {
      this->target->extra = true;
      return JSON::NodeSink::addObject(rawname, name);
    }
    std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name) {
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
    void addNumber(BString &rawname, BString &name, BString &raw,
                   JSON::number value) {
      this->target->extra = true;
    }
    void addBool(BString &rawname, BString &name, bool value) {
      this->target->extra = true;
    }
    void addNull(BString &rawname, BString &name) {
      this->target->extra = true;
    }
    void addString(BString &rawname, BString &name, BString &raw,
                   BString &value) {
      this->target->extra = true;
    }
    std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name) {
      return std::make_unique<OExpect>(this->target, this->ix++);
    }
    std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name) {
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
    void addNumber(BString &rawname, BString &name, BString &raw,
                   JSON::number value) {
      this->target->extra = true;
    }
    void addBool(BString &rawname, BString &name, bool value) {
      this->target->extra = true;
    }
    void addNull(BString &rawname, BString &name) {
      this->target->extra = true;
    }
    void addString(BString &rawname, BString &name, BString &raw,
                   BString &value) {
      this->target->extra = true;
    }
    std::unique_ptr<NodeSink> addObject(BString &rawname, BString &name) {
      this->target->extra = true;
      return JSON::NodeSink::addObject(rawname, name);
    }
    std::unique_ptr<NodeSink> addArray(BString &rawname, BString &name) {
      return std::make_unique<AExpect>(this->target);
    }

  private:
    Results *target;
  };

  Results results;
  char example[] = "[{\"a\":\"b\",\"c\":\"d\"},{\"e\":\"f\"}]";
  {
    JSON::Parser parser(std::make_unique<RootExpect>(&results));
    for (int i = 0; i < sizeof(example); i++) {
      parser.nextChar(example[i]);
    }
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

TEST_CASE("Correctly serialises 0", "[JSON]") {
  BString target;
  {
    JSON::RootSink rootSink(std::make_unique<JSON::SerializerStart>(&target));
    BString blank;
    rootSink.addNumber(blank, 0);
  }
  REQUIRE(target == "0");
}