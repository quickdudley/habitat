#ifndef CONNECTION_H
#define CONNECTION_H

#include "Secret.h"
#include <DataIO.h>
#include <Handler.h>
#include <Messenger.h>
#include <String.h>
#include <memory>
#include <set>
#include <sodium.h>

extern const unsigned char SSB_NETWORK_ID[32];

enum HandshakeError {
  KEYGEN_FAIL,
  HMAC_FAIL,
  HANDSHAKE_HANGUP,
  WRONG_NETKEY,
  SECRET_FAILED
};

class BoxStream : public BDataIO {
public:
  // Client side of handshake (server public key known)
  BoxStream(std::unique_ptr<BDataIO> inner, const unsigned char netkey[32],
            Ed25519Secret *myId, const unsigned char srvkey[32]);
  // Server side of handshake (will receive client public key)
  BoxStream(std::unique_ptr<BDataIO> inner, const unsigned char netkey[32],
            Ed25519Secret *myId);
  // TODO: Move goodbye into separate method and just call it from destructor
  ~BoxStream();
  ssize_t Write(const void *buffer, size_t size) override;
  ssize_t Read(void *buffer, size_t size) override;
  status_t Flush() override;
  BString cypherkey();
  void getPeerKey(unsigned char out[crypto_sign_PUBLICKEYBYTES]);

private:
  std::unique_ptr<BDataIO> inner;
  std::unique_ptr<unsigned char> read_buffer;
  size_t rb_length = 0;
  size_t rb_offset = 0;
  unsigned char peerkey[crypto_sign_PUBLICKEYBYTES];
  unsigned char sendkey[32];
  unsigned char sendnonce[24];
  unsigned char recvkey[32];
  unsigned char recvnonce[24];
};

class ConnectedList : public BHandler {
public:
  void MessageReceived(BMessage *message) override;
  void addConnected(const BString &key);
  void addExcluded(const BString &key);
  void rmConnected(const BString &key);
  bool checkConnected(const BString &key);
  std::set<BString> getConnected();
  static ConnectedList *instance();

private:
  bool _checkConnected(const BString &key);
  std::set<BString> _getConnected();
  std::set<BString> connected;
  std::set<BString> excluded;
};

enum PortOption { PORT_FORBIDDEN = 0, PORT_OPTIONAL = 1, PORT_REQUIRED = 2 };

bool validateCypherkey(const BString &key);

bool validateHostname(const BString &hostName,
                      PortOption portFlags = PORT_FORBIDDEN);
#endif
