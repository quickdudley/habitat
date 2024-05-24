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

TEST_CASE("Correctly parses message that previously failed validattion", "[BJSON]") {
  char sample[] = "{\"previous\":\"%Y0SZGE3wmW8f86MWe/sXhRz+KDLrFFPNyaEIWgq9XMc=.sha256\",\"sequence\":38,\"author\":\"@jX1pC2ZWcoOd0z8IrVxpBX7bpcPfV438t6jsvp3d9Po=.ed25519\",\"timestamp\":1599477571853.0059,\"hash\":\"sha256\",\"content\":{\"type\":\"tag\",\"version\":1,\"tagged\":true,\"message\":\"%QnteO6UkWsbWl7ZgK0yN1ShzbxWoXqYbUEx/TduZCXA=.sha256\",\"root\":\"%XLqP4zuQz520UZBgAwufdsfTeoek7YsGWJ4O1h1WBIM=.sha256\",\"branch\":[]},\"signature\":\"qJ9IrKlnMqUNxe+RoXtzGsQ3Xfa4six6SMLuarLyefiw7lIeXq8m7J3Pqwed9x01CsVCOFkQhlplOukPNjmUBA==.sig.ed25519\"}";
  BMessage output;
  REQUIRE(JSON::parse(std::make_unique<JSON::BMessageDocSink>(&output), sample) ==
    B_OK);
  BString s;
  double n;
  bool b;
  REQUIRE(output.FindString("previous", &s) == B_OK);
  REQUIRE(s == "%Y0SZGE3wmW8f86MWe/sXhRz+KDLrFFPNyaEIWgq9XMc=.sha256");
  REQUIRE(output.FindDouble("sequence", &n) == B_OK);
  REQUIRE(n == 38);
  REQUIRE(output.FindString("author", &s) == B_OK);
  REQUIRE(s == "@jX1pC2ZWcoOd0z8IrVxpBX7bpcPfV438t6jsvp3d9Po=.ed25519");
  REQUIRE(output.FindDouble("timestamp", &n) == B_OK);
  REQUIRE(n == 1599477571853.0059);
  REQUIRE(output.FindString("hash", &s) == B_OK);
  REQUIRE(s == "sha256");
  BMessage content;
  REQUIRE(output.FindMessage("content", &content) == B_OK);
  REQUIRE(content.FindString("type", &s) == B_OK);
  REQUIRE(s == "tag");
  REQUIRE(content.FindDouble("version", &n) == B_OK);
  REQUIRE(n == 1);
  REQUIRE(content.FindBool("tagged", &b) == B_OK);
  REQUIRE(b == true);
  REQUIRE(content.FindString("message", &s) == B_OK);
  REQUIRE(s == "%QnteO6UkWsbWl7ZgK0yN1ShzbxWoXqYbUEx/TduZCXA=.sha256");
  REQUIRE(content.FindString("root", &s) == B_OK);
  REQUIRE(s == "%XLqP4zuQz520UZBgAwufdsfTeoek7YsGWJ4O1h1WBIM=.sha256");
  BMessage branch;
  REQUIRE(content.FindMessage("branch", &branch) == B_OK);
  REQUIRE(output.FindString("signature", &s) == B_OK);
  REQUIRE(s == "qJ9IrKlnMqUNxe+RoXtzGsQ3Xfa4six6SMLuarLyefiw7lIeXq8m7J3Pqwed9x01CsVCOFkQhlplOukPNjmUBA==.sig.ed25519");
}
