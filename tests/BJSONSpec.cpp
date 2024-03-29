#include "BJSON.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Can represent two objects in arrays", "[BJSON]") {
  BMessage target;
  {
    JSON::RootSink sink(std::make_unique<JSON::BMessageDocSink>(&target));
    BString k, v;
    sink.beginArray(k);
    sink.beginObject(k);
    k = "a";
    v = "b";
    sink.addString(k, v);
    k = "c";
    v = "d";
    sink.addString(k, v);
    sink.closeNode();
    sink.beginObject(k);
    k = "e";
    v = "f";
    sink.addString(k, v);
    sink.closeNode();
    sink.closeNode();
  }
  REQUIRE(target.what == 'JSAR');
  BMessage inner;
  REQUIRE(target.FindMessage("0", &inner) == B_OK);
  BString value;
  REQUIRE(inner.FindString("a", &value) == B_OK);
  REQUIRE(value == "b");
  REQUIRE(inner.FindString("c", &value) == B_OK);
  REQUIRE(value == "d");
  REQUIRE(inner.FindString("e", &value) != B_OK);
  REQUIRE(inner.FindString("b", &value) != B_OK);
  REQUIRE(target.FindMessage("1", &inner) == B_OK);
  REQUIRE(inner.FindString("a", &value) != B_OK);
  REQUIRE(inner.FindString("e", &value) == B_OK);
  REQUIRE(value == "f");
}

TEST_CASE("Includes outermost braces when serialising BMessage to JSON",
          "[BJSON]") {
  BString output;
  BMessage message('JSOB');
  {
    JSON::RootSink rootSink(std::make_unique<JSON::SerializerStart>(&output));
    JSON::fromBMessage(&rootSink, &message);
  }
  REQUIRE(output == "{}");
  output = "";
  message.what = 'JSAR';
  {
    JSON::RootSink rootSink(std::make_unique<JSON::SerializerStart>(&output));
    JSON::fromBMessage(&rootSink, &message);
  }
  REQUIRE(output == "[]");
}
