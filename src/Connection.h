#ifndef CONNECTION_H
#define CONNECTION_H

#include <DataIO.h>
#include <memory>
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
  BoxStream(std::unique_ptr<BDataIO> inner, unsigned char netkey[32],
            unsigned char privkey[32], unsigned char seckey[32],
            unsigned char srvkey[32]);
  // Server side of handshake (will receive client public key)
  BoxStream(std::unique_ptr<BDataIO> inner, unsigned char netkey[32],
            unsigned char seckey[32], unsigned char pubkey[32]);
  ssize_t Write(const void *buffer, size_t size);
  ssize_t Read(void *buffer, size_t size);
  status_t Flush();

private:
  std::unique_ptr<BDataIO> inner;
  std::unique_ptr<unsigned char> read_buffer;
  size_t rb_length = 0;
  size_t rb_offset = 0;
  unsigned char peerkey[32];
  unsigned char sendkey[32];
  unsigned char sendnonce[24];
  unsigned char recvkey[32];
  unsigned char recvnonce[24];
};

#endif
