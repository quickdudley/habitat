#include "Connection.h"
#include <OS.h>
#include <Socket.h>
#include <catch2/catch_all.hpp>

namespace {
class PairedSocket : public BSocket {
public:
  static void makePair(BAbstractSocket *&a, BAbstractSocket *&b);

private:
  PairedSocket(int number);
};

PairedSocket::PairedSocket(int number) {
  this->fInitStatus = B_OK;
  this->fSocket = number;
}

void PairedSocket::makePair(BAbstractSocket *&a, BAbstractSocket *&b) {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
    throw "Failed to create socket pair!";
  a = new PairedSocket(sv[0]);
  b = new PairedSocket(sv[1]);
}

struct TestData {
  unsigned char netkey[crypto_auth_KEYBYTES];
  Ed25519Secret serverKeys;
  Ed25519Secret clientKeys;
  char c2s[6];
  char s2c[6];
  char sgot[6];
  char cgot[6];
  BAbstractSocket *s;
  BAbstractSocket *c;
};

int32 serverThread(void *data) {
  std::unique_ptr<BDataIO> conn = std::make_unique<BoxStream>(
      std::unique_ptr<BDataIO>(((TestData *)data)->s),
      ((TestData *)data)->netkey, &((TestData *)data)->serverKeys);
  conn->ReadExactly(((TestData *)data)->sgot, sizeof(((TestData *)data)->sgot));
  conn->WriteExactly(((TestData *)data)->s2c, sizeof(((TestData *)data)->s2c));
  return 0;
}

int32 clientThread(void *data) {
  std::unique_ptr<BDataIO> conn = std::make_unique<BoxStream>(
      std::unique_ptr<BDataIO>(((TestData *)data)->c),
      ((TestData *)data)->netkey, &((TestData *)data)->clientKeys,
      ((TestData *)data)->serverKeys.pubkey);
  conn->WriteExactly(((TestData *)data)->c2s, sizeof(((TestData *)data)->c2s));
  conn->ReadExactly(((TestData *)data)->cgot, sizeof(((TestData *)data)->cgot));
  return 0;
}
} // namespace

TEST_CASE("SHS connection works", "[shs]") {
  TestData testdata;
  crypto_auth_keygen(testdata.netkey);
  testdata.serverKeys.generate();
  testdata.clientKeys.generate();
  memcpy(testdata.c2s, "Hello", 6);
  memcpy(testdata.s2c, "World", 6);
  memset(testdata.sgot, 0, 6);
  memset(testdata.cgot, 0, 6);
  PairedSocket::makePair(testdata.s, testdata.c);
  thread_id srv = spawn_thread(serverThread, "Test SHS server",
                               B_NORMAL_PRIORITY, (void *)&testdata);
  thread_id client = spawn_thread(clientThread, "Test SHS client",
                                  B_NORMAL_PRIORITY, (void *)&testdata);
  resume_thread(srv);
  resume_thread(client);
  status_t exitValue;
  wait_for_thread(srv, &exitValue);
  REQUIRE(exitValue == B_OK);
  wait_for_thread(client, &exitValue);
  REQUIRE(exitValue == B_OK);
  REQUIRE(BString(testdata.c2s) == "Hello");
  REQUIRE(BString(testdata.s2c) == "World");
  REQUIRE(BString(testdata.sgot) == "Hello");
  REQUIRE(BString(testdata.cgot) == "World");
}

TEST_CASE("Hostname validation validation", "[net]") {
  REQUIRE(validateHostname("127.0.0.1"));
  REQUIRE(validateHostname("127.0.0.1", PORT_OPTIONAL));
  REQUIRE_FALSE(validateHostname("127.0.0.1", PORT_REQUIRED));
  REQUIRE_FALSE(validateHostname("127.0.0.1:8008"));
  REQUIRE(validateHostname("127.0.0.1:8008", PORT_OPTIONAL));
  REQUIRE(validateHostname("127.0.0.1:8008", PORT_REQUIRED));
  REQUIRE_FALSE(validateHostname("127.1", PORT_OPTIONAL));
  REQUIRE(validateHostname("127.0.0.1."));
  REQUIRE(validateHostname("127.0.0.1.:8008", PORT_REQUIRED));
  REQUIRE(validateHostname("::1:8008", PORT_REQUIRED));
  REQUIRE_FALSE(validateHostname("::1:8008", PORT_OPTIONAL));
  REQUIRE(validateHostname("[::1]:8008", PORT_OPTIONAL));
  REQUIRE_FALSE(validateHostname("[::1]:8008", PORT_FORBIDDEN));
  REQUIRE(validateHostname("[::1]:8008", PORT_REQUIRED));
  REQUIRE_FALSE(validateHostname("1:1:1:1:1:1:1:1:1", PORT_FORBIDDEN));
  REQUIRE_FALSE(validateHostname("1:1:1:1::1:1:1", PORT_FORBIDDEN));
  REQUIRE(validateHostname("1:1:1:1::1:1", PORT_FORBIDDEN));
  REQUIRE(validateHostname("example.com", PORT_FORBIDDEN));
  REQUIRE(validateHostname("example.com", PORT_OPTIONAL));
  REQUIRE_FALSE(validateHostname("example.com", PORT_REQUIRED));
}
