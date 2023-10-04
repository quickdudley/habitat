#include "Connection.h"
#include "Base64.h"
#include <Errors.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <support/ByteOrder.h>
#include <utility>

const unsigned char SSB_NETWORK_ID[32] = {
    0xd4, 0xa1, 0xcb, 0x88, 0xa6, 0x6f, 0x02, 0xf8, 0xdb, 0x63, 0x5c,
    0xe2, 0x64, 0x41, 0xcc, 0x5d, 0xac, 0x1b, 0x08, 0x42, 0x0c, 0xea,
    0xac, 0x23, 0x08, 0x39, 0xb7, 0x55, 0x84, 0x5a, 0x9f, 0xfb};

static void
sendHello(BDataIO *peer, const unsigned char netkey[crypto_auth_KEYBYTES],
          const unsigned char ephemeralPK[crypto_box_PUBLICKEYBYTES],
          unsigned char *nonce) {
  unsigned char buffer[crypto_auth_BYTES + crypto_box_PUBLICKEYBYTES];
#define sendingAuth (buffer + 0)
#define sendingKey (sendingAuth + crypto_auth_BYTES)
  memcpy(sendingKey, ephemeralPK, crypto_sign_PUBLICKEYBYTES);
  if (crypto_auth(sendingAuth, sendingKey, crypto_box_PUBLICKEYBYTES, netkey) <
      0)
    throw HMAC_FAIL;
  if (peer->WriteExactly(buffer, sizeof(buffer), NULL) != B_OK)
    throw HANDSHAKE_HANGUP;
  memcpy(nonce, sendingAuth, crypto_secretbox_NONCEBYTES);
#undef sendingKey
#undef sendingAuth
}

static void readHello(BDataIO *peer,
                      const unsigned char netkey[crypto_auth_KEYBYTES],
                      unsigned char *ephemeralKey, unsigned char *nonce) {
  unsigned char buffer[crypto_auth_BYTES + crypto_box_PUBLICKEYBYTES];
  if (peer->ReadExactly(buffer, sizeof(buffer), NULL) != B_OK)
    throw HANDSHAKE_HANGUP;
#define receivedAuth (buffer + 0)
#define receivedKey (receivedAuth + crypto_auth_BYTES)
  if (crypto_auth_verify(receivedAuth, receivedKey, crypto_box_PUBLICKEYBYTES,
                         netkey) < 0)
    throw WRONG_NETKEY;
  memcpy(ephemeralKey, receivedKey, crypto_sign_PUBLICKEYBYTES);
  memcpy(nonce, receivedAuth, crypto_secretbox_NONCEBYTES);
#undef receivedKey
#undef receivedAuth
}

static void
clientAuthKey(unsigned char *boxkey,
              const unsigned char netkey[crypto_auth_KEYBYTES],
              const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
              const unsigned char sharedSecretaB[crypto_scalarmult_BYTES]) {
  crypto_hash_sha256_state state;
  if (crypto_hash_sha256_init(&state) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, netkey, crypto_auth_KEYBYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, sharedSecretab,
                                crypto_scalarmult_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, sharedSecretaB,
                                crypto_scalarmult_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_final(&state, boxkey) < 0)
    throw SECRET_FAILED;
}

static void
serverAcceptKey(unsigned char *boxkey,
                const unsigned char netkey[crypto_auth_KEYBYTES],
                const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
                const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
                const unsigned char sharedSecretAb[crypto_scalarmult_BYTES]) {
  crypto_hash_sha256_state state;
  if (crypto_hash_sha256_init(&state) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, netkey, crypto_auth_KEYBYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, sharedSecretab,
                                crypto_scalarmult_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, sharedSecretaB,
                                crypto_scalarmult_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, sharedSecretAb,
                                crypto_scalarmult_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_final(&state, boxkey) < 0)
    throw SECRET_FAILED;
}

static inline void
sendClientAuth(BDataIO *peer, const unsigned char netkey[crypto_auth_KEYBYTES],
               const unsigned char serverKey[crypto_sign_PUBLICKEYBYTES],
               const unsigned char clientSecret[crypto_sign_SECRETKEYBYTES],
               const unsigned char clientKey[crypto_sign_PUBLICKEYBYTES],
               const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
               const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
               unsigned char *savedSignature) {
  unsigned char boxBuffer[crypto_secretbox_MACBYTES + crypto_sign_BYTES +
                          crypto_sign_PUBLICKEYBYTES];
#define detachedSignature (boxBuffer + crypto_secretbox_MACBYTES)
#define sendingKey (detachedSignature + crypto_sign_BYTES)
  {
    unsigned char signBuffer[crypto_auth_KEYBYTES + crypto_sign_PUBLICKEYBYTES +
                             crypto_hash_sha256_BYTES];
#define signingNetid (signBuffer + 0)
#define signingServerKey (signingNetid + crypto_auth_KEYBYTES)
#define signingSharedSecret (signingServerKey + crypto_sign_PUBLICKEYBYTES)
    if (crypto_hash_sha256(signingSharedSecret, sharedSecretab,
                           crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    memcpy(signingNetid, netkey, crypto_auth_KEYBYTES);
    memcpy(signingServerKey, serverKey, crypto_sign_PUBLICKEYBYTES);
#undef signingSharedSecret
#undef signingServerKey
#undef signingNetid
    if (crypto_sign_detached(detachedSignature, NULL, signBuffer,
                             sizeof(signBuffer), clientSecret) < 0)
      throw SECRET_FAILED;
    memcpy(savedSignature, detachedSignature, crypto_sign_BYTES);
  }
  memcpy(sendingKey, clientKey, crypto_sign_PUBLICKEYBYTES);
  {
    unsigned char boxkey[crypto_hash_sha256_BYTES];
    clientAuthKey(boxkey, netkey, sharedSecretab, sharedSecretaB);
    unsigned char nonce[crypto_box_NONCEBYTES];
    memset(nonce, 0, sizeof(nonce));
    if (crypto_secretbox_easy(boxBuffer, boxBuffer + crypto_secretbox_MACBYTES,
                              sizeof(boxBuffer) - crypto_secretbox_MACBYTES,
                              nonce, boxkey) < 0)
      throw SECRET_FAILED;
  }
#undef sendingKey
#undef detachedSignature
  if (peer->WriteExactly(boxBuffer, sizeof(boxBuffer), NULL) != B_OK)
    throw HANDSHAKE_HANGUP;
}

static inline void
readClientAuth(BDataIO *peer, const unsigned char netkey[crypto_auth_KEYBYTES],
               const unsigned char serverKey[crypto_sign_PUBLICKEYBYTES],
               unsigned char *clientKey,
               const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
               const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
               unsigned char *savedSignature) {
  unsigned char buffer[crypto_secretbox_MACBYTES + crypto_sign_BYTES +
                       crypto_sign_PUBLICKEYBYTES];
  if (peer->ReadExactly(buffer, sizeof(buffer), NULL) != B_OK)
    throw HANDSHAKE_HANGUP;
  {
    unsigned char key[crypto_hash_sha256_BYTES];
    clientAuthKey(key, netkey, sharedSecretab, sharedSecretaB);
    unsigned char nonce[crypto_box_NONCEBYTES];
    memset(nonce, 0, crypto_box_NONCEBYTES);
    if (crypto_secretbox_open_easy(buffer + crypto_secretbox_MACBYTES, buffer,
                                   sizeof(buffer), nonce, key) < 0)
      throw SECRET_FAILED;
  }
#define detachedSignature (buffer + crypto_secretbox_MACBYTES)
#define receivedClientKey (detachedSignature + crypto_sign_BYTES)
  {
    unsigned char verifyBuffer[crypto_auth_KEYBYTES +
                               crypto_sign_PUBLICKEYBYTES +
                               crypto_hash_sha256_BYTES];
#define verifyNetkey (verifyBuffer + 0)
#define verifyServerKey (verifyNetkey + crypto_auth_KEYBYTES)
#define verifySharedSecret (verifyServerKey + crypto_sign_PUBLICKEYBYTES)
    if (crypto_hash_sha256(verifySharedSecret, sharedSecretab,
                           crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    memcpy(verifyNetkey, netkey, crypto_auth_KEYBYTES);
    memcpy(verifyServerKey, serverKey, crypto_sign_PUBLICKEYBYTES);
#undef verifySharedSecret
#undef verifyServerKey
#undef verifyNetkey
    if (crypto_sign_verify_detached(detachedSignature, verifyBuffer,
                                    sizeof(verifyBuffer),
                                    receivedClientKey) != 0)
      throw SECRET_FAILED;
    memcpy(savedSignature, detachedSignature, crypto_sign_BYTES);
  }
  memcpy(clientKey, receivedClientKey, crypto_sign_PUBLICKEYBYTES);
#undef receivedClientKey
#undef detachedSignature
}

static inline void
sendServerAccept(BDataIO *peer,
                 const unsigned char netkey[crypto_auth_KEYBYTES],
                 const unsigned char serverSecret[crypto_sign_SECRETKEYBYTES],
                 const unsigned char clientKey[crypto_sign_PUBLICKEYBYTES],
                 const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
                 const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
                 const unsigned char sharedSecretAb[crypto_scalarmult_BYTES],
                 const unsigned char signatureA[crypto_sign_BYTES]) {
  unsigned char boxBuffer[crypto_secretbox_MACBYTES + crypto_sign_BYTES];
#define signature (boxBuffer + crypto_secretbox_MACBYTES)
  {
    unsigned char signBuffer[crypto_auth_KEYBYTES + crypto_sign_BYTES +
                             crypto_sign_PUBLICKEYBYTES +
                             crypto_hash_sha256_BYTES];
#define signingNetkey (signBuffer + 0)
#define signingSignature (signingNetkey + crypto_auth_KEYBYTES)
#define signingClientKey (signingSignature + crypto_sign_BYTES)
#define signingSharedSecret (signingClientKey + crypto_sign_PUBLICKEYBYTES)
    if (crypto_hash_sha256(signingSharedSecret, sharedSecretab,
                           crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    memcpy(signingNetkey, netkey, crypto_auth_KEYBYTES);
    memcpy(signingSignature, signatureA, crypto_sign_BYTES);
    memcpy(signingClientKey, clientKey, crypto_sign_PUBLICKEYBYTES);
#undef signingSharedSecret
#undef signingClientKey
#undef signingSignature
#undef signingNetkey
    if (crypto_sign_detached(signature, NULL, signBuffer, sizeof(signBuffer),
                             serverSecret) < 0)
      throw SECRET_FAILED;
  }
  {
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    unsigned char boxkey[crypto_hash_sha256_BYTES];
    serverAcceptKey(boxkey, netkey, sharedSecretab, sharedSecretaB,
                    sharedSecretAb);
    memset(nonce, 0, sizeof(nonce));
    if (crypto_secretbox_easy(boxBuffer, boxBuffer + crypto_secretbox_MACBYTES,
                              sizeof(boxBuffer) - crypto_secretbox_MACBYTES,
                              nonce, boxkey) < 0)
      throw SECRET_FAILED;
  }
  size_t sent;
  if (peer->WriteExactly(boxBuffer, sizeof(boxBuffer), &sent) != B_OK)
    throw HANDSHAKE_HANGUP;
}

static inline void
readServerAccept(BDataIO *peer,
                 const unsigned char netkey[crypto_auth_KEYBYTES],
                 const unsigned char serverKey[crypto_sign_PUBLICKEYBYTES],
                 const unsigned char clientKey[crypto_sign_PUBLICKEYBYTES],
                 const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
                 const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
                 const unsigned char sharedSecretAb[crypto_scalarmult_BYTES],
                 const unsigned char signatureA[crypto_sign_BYTES]) {
  unsigned char boxBuffer[crypto_secretbox_MACBYTES + crypto_sign_BYTES];
  if (peer->ReadExactly(boxBuffer, sizeof(boxBuffer), NULL) != B_OK)
    throw HANDSHAKE_HANGUP;
  {
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    unsigned char boxkey[crypto_hash_sha256_BYTES];
    serverAcceptKey(boxkey, netkey, sharedSecretab, sharedSecretaB,
                    sharedSecretAb);
    memset(nonce, 0, sizeof(nonce));
    if (crypto_secretbox_open_easy(boxBuffer + crypto_secretbox_MACBYTES,
                                   boxBuffer, sizeof(boxBuffer), nonce,
                                   boxkey) < 0)
      throw SECRET_FAILED;
  }
#define detachedSignature (boxBuffer + crypto_secretbox_MACBYTES)
  {
    unsigned char verifyBuffer[crypto_auth_KEYBYTES + crypto_sign_BYTES +
                               crypto_sign_PUBLICKEYBYTES +
                               crypto_hash_sha256_BYTES];
#define verifyNetkey (verifyBuffer + 0)
#define verifyFirstSignature (verifyNetkey + crypto_auth_KEYBYTES)
#define verifyClientKey (verifyFirstSignature + crypto_sign_BYTES)
#define verifySharedSecret (verifyClientKey + crypto_sign_PUBLICKEYBYTES)
    if (crypto_hash_sha256(verifySharedSecret, sharedSecretab,
                           crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    memcpy(verifyNetkey, netkey, crypto_auth_KEYBYTES);
    memcpy(verifyFirstSignature, signatureA, crypto_sign_BYTES);
    memcpy(verifyClientKey, clientKey, crypto_sign_PUBLICKEYBYTES);
#undef verifySharedSecret
#undef verifyClientKey
#undef verifyFirstSignature
#undef verifyNetkey
    if (crypto_sign_verify_detached(detachedSignature, verifyBuffer,
                                    sizeof(verifyBuffer), serverKey) < 0)
      throw SECRET_FAILED;
  }
#undef detachedSignature
}

static void
streamKeyCommon(unsigned char *output,
                const unsigned char netkey[crypto_auth_KEYBYTES],
                const unsigned char sharedSecretab[crypto_scalarmult_BYTES],
                const unsigned char sharedSecretaB[crypto_scalarmult_BYTES],
                const unsigned char sharedSecretAb[crypto_scalarmult_BYTES]) {
  unsigned char hash1[crypto_hash_sha256_BYTES];
  {
    crypto_hash_sha256_state state;
    if (crypto_hash_sha256_init(&state) < 0)
      throw SECRET_FAILED;
    if (crypto_hash_sha256_update(&state, netkey, crypto_auth_KEYBYTES) < 0)
      throw SECRET_FAILED;
    if (crypto_hash_sha256_update(&state, sharedSecretab,
                                  crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    if (crypto_hash_sha256_update(&state, sharedSecretaB,
                                  crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    if (crypto_hash_sha256_update(&state, sharedSecretAb,
                                  crypto_scalarmult_BYTES) < 0)
      throw SECRET_FAILED;
    if (crypto_hash_sha256_final(&state, hash1) < 0)
      throw SECRET_FAILED;
  }
  if (crypto_hash_sha256(output, hash1, crypto_hash_sha256_BYTES) < 0)
    throw SECRET_FAILED;
}

static void streamKey(unsigned char *output,
                      const unsigned char common[crypto_hash_sha256_BYTES],
                      const unsigned char peerKey[crypto_sign_PUBLICKEYBYTES]) {
  crypto_hash_sha256_state state;
  if (crypto_hash_sha256_init(&state) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, common, crypto_hash_sha256_BYTES) < 0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_update(&state, peerKey, crypto_sign_PUBLICKEYBYTES) <
      0)
    throw SECRET_FAILED;
  if (crypto_hash_sha256_final(&state, output) < 0)
    throw SECRET_FAILED;
}

// Client side of handshake (server public key known)
BoxStream::BoxStream(std::unique_ptr<BDataIO> inner,
                     const unsigned char netkey[crypto_auth_KEYBYTES],
                     Ed25519Secret *myId,
                     const unsigned char srvkey[crypto_sign_PUBLICKEYBYTES]) {
  this->inner = std::move(inner);
  unsigned char ephemeralKey[crypto_box_PUBLICKEYBYTES];
  unsigned char ephemeralSecret[crypto_box_SECRETKEYBYTES];
  if (crypto_box_keypair(ephemeralKey, ephemeralSecret) < 0)
    throw KEYGEN_FAIL;
  memcpy(this->peerkey, srvkey, crypto_sign_PUBLICKEYBYTES);
  sendHello(this->inner.get(), netkey, ephemeralKey, this->recvnonce);
  unsigned char serverEphemeral[crypto_box_PUBLICKEYBYTES];
  readHello(this->inner.get(), netkey, serverEphemeral, this->sendnonce);
  unsigned char sharedSecretab[crypto_scalarmult_BYTES];
  if (crypto_scalarmult(sharedSecretab, ephemeralSecret, serverEphemeral) < 0)
    throw SECRET_FAILED;
  unsigned char sharedSecretaB[crypto_scalarmult_BYTES];
  {
    unsigned char converted[crypto_scalarmult_BYTES];
    if (crypto_sign_ed25519_pk_to_curve25519(converted, srvkey) < 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(sharedSecretaB, ephemeralSecret, converted) < 0)
      throw SECRET_FAILED;
  }
  unsigned char signatureA[crypto_sign_BYTES];
  sendClientAuth(this->inner.get(), netkey, srvkey, myId->secret, myId->pubkey,
                 sharedSecretab, sharedSecretaB, signatureA);
  unsigned char sharedSecretAb[crypto_scalarmult_BYTES];
  {
    unsigned char converted[crypto_scalarmult_BYTES];
    if (crypto_sign_ed25519_sk_to_curve25519(converted, myId->secret) != 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(sharedSecretAb, converted, serverEphemeral) != 0)
      throw SECRET_FAILED;
  }
  readServerAccept(this->inner.get(), netkey, srvkey, myId->pubkey,
                   sharedSecretab, sharedSecretaB, sharedSecretAb, signatureA);
  unsigned char commonStreamKey[crypto_hash_sha256_BYTES];
  streamKeyCommon(commonStreamKey, netkey, sharedSecretab, sharedSecretaB,
                  sharedSecretAb);
  streamKey(this->sendkey, commonStreamKey, srvkey);
  streamKey(this->recvkey, commonStreamKey, myId->pubkey);
}

// Server side of handshake (will receive client public key)
BoxStream::BoxStream(std::unique_ptr<BDataIO> inner,
                     const unsigned char netkey[crypto_auth_KEYBYTES],
                     Ed25519Secret *myId) {
  this->inner = std::move(inner);
  unsigned char ephemeralKey[crypto_box_PUBLICKEYBYTES];
  unsigned char ephemeralSecret[crypto_box_SECRETKEYBYTES];
  if (crypto_box_keypair(ephemeralKey, ephemeralSecret) < 0)
    throw KEYGEN_FAIL;
  unsigned char clientEphemeral[crypto_box_PUBLICKEYBYTES];
  readHello(this->inner.get(), netkey, clientEphemeral, this->sendnonce);
  unsigned char sharedSecretab[crypto_scalarmult_BYTES];
  if (crypto_scalarmult(sharedSecretab, ephemeralSecret, clientEphemeral) != 0)
    throw SECRET_FAILED;
  unsigned char sharedSecretaB[crypto_scalarmult_BYTES];
  {
    unsigned char converted[crypto_secretbox_KEYBYTES];
    if (crypto_sign_ed25519_sk_to_curve25519(converted, myId->secret) != 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(sharedSecretaB, converted, clientEphemeral) != 0)
      throw SECRET_FAILED;
  }
  sendHello(this->inner.get(), netkey, ephemeralKey, this->recvnonce);
  unsigned char signatureA[crypto_sign_BYTES];
  readClientAuth(this->inner.get(), netkey, myId->pubkey, this->peerkey,
                 sharedSecretab, sharedSecretaB, signatureA);
  unsigned char sharedSecretAb[crypto_scalarmult_BYTES];
  {
    unsigned char converted[crypto_box_PUBLICKEYBYTES];
    if (crypto_sign_ed25519_pk_to_curve25519(converted, this->peerkey) != 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(sharedSecretAb, ephemeralSecret, converted) != 0)
      throw SECRET_FAILED;
  }
  sendServerAccept(this->inner.get(), netkey, myId->secret, this->peerkey,
                   sharedSecretab, sharedSecretaB, sharedSecretAb, signatureA);
  unsigned char commonStreamKey[crypto_hash_sha256_BYTES];
  streamKeyCommon(commonStreamKey, netkey, sharedSecretab, sharedSecretaB,
                  sharedSecretAb);
  streamKey(this->sendkey, commonStreamKey, this->peerkey);
  streamKey(this->recvkey, commonStreamKey, myId->pubkey);
}

static void nonce_inc(unsigned char *nonce) {
  unsigned char i = crypto_secretbox_NONCEBYTES - 1;
  while (i != 255) {
    nonce[i] += 1;
    if (nonce[i] == 0) {
      i--;
    } else {
      break;
    }
  }
}

BoxStream::~BoxStream() {
  unsigned char header[crypto_secretbox_MACBYTES + 18];
  memset(header + crypto_secretbox_MACBYTES, 0, 18);
  nonce_inc(this->sendnonce);
  if (crypto_secretbox_easy(header, header + crypto_secretbox_MACBYTES, 18,
                            this->sendnonce, this->sendkey) == 0)
    this->inner->WriteExactly(header, crypto_secretbox_MACBYTES + 18, NULL);
}

ssize_t BoxStream::Write(const void *buffer, size_t size) {
  if (size > 4096) {
    size = 4096;
  }
  std::unique_ptr<unsigned char> buffer1 =
      std::unique_ptr<unsigned char>(new unsigned char[size + 34]);
  unsigned char tmpnonce[24];
  memcpy(tmpnonce, this->sendnonce, 24);
  nonce_inc(tmpnonce);
  if (crypto_secretbox_easy(buffer1.get() + 18, (unsigned char *)buffer, size,
                            tmpnonce, this->sendkey) != 0)
    return B_IO_ERROR;
  *((unsigned short *)(buffer1.get() + 16)) = (unsigned short)size;
  if (swap_data(B_INT16_TYPE, buffer1.get() + 16, sizeof(short),
                B_SWAP_HOST_TO_BENDIAN) != B_OK)
    return B_IO_ERROR;
  if (crypto_secretbox_easy(buffer1.get(), buffer1.get() + 16, 18,
                            this->sendnonce, this->sendkey) != 0)
    return B_IO_ERROR;
  nonce_inc(tmpnonce);
  memcpy(this->sendnonce, tmpnonce, 24);
  size_t bytes_written;
  status_t result =
      this->inner->WriteExactly(buffer1.get(), size + 34, &bytes_written);
  if (result == B_OK) {
    return bytes_written - 34;
  } else {
    return result;
  }
}

ssize_t BoxStream::Read(void *buffer, size_t size) {
  size_t unread = this->rb_length - this->rb_offset;
  if (unread == 0) {
    unsigned char header[34];
    unsigned char *headerMsg = header + crypto_secretbox_MACBYTES;
    status_t err;
    size_t received;
    if ((err = this->inner->ReadExactly(header, sizeof(header), &received)) !=
        B_OK) {
      return B_IO_ERROR;
    }
    if (crypto_secretbox_open_easy(headerMsg, header, 34, this->recvnonce,
                                   this->recvkey) != 0) {
      return B_IO_ERROR;
    }
    nonce_inc(this->recvnonce);
    if (swap_data(B_INT16_TYPE, headerMsg, sizeof(short),
                  B_SWAP_BENDIAN_TO_HOST) != B_OK) {
      return B_IO_ERROR;
    }
    size_t bodyLength = (size_t) * ((short *)headerMsg);
    this->read_buffer =
        std::unique_ptr<unsigned char>(new unsigned char[bodyLength + 16]);
    memcpy(this->read_buffer.get(), headerMsg + 2, 16);
    if (inner->ReadExactly(this->read_buffer.get() + 16, bodyLength) != B_OK) {
      return B_IO_ERROR;
    }
    if (crypto_secretbox_open_easy(this->read_buffer.get() + 16,
                                   this->read_buffer.get(), bodyLength + 16,
                                   this->recvnonce, this->recvkey) != 0) {
      return B_IO_ERROR;
    }
    nonce_inc(this->recvnonce);
    unread = bodyLength;
    this->rb_length = bodyLength + 16;
    this->rb_offset = 16;
  }
  size_t readNow = std::min(unread, size);
  memcpy(buffer, this->read_buffer.get() + this->rb_offset, readNow);
  this->rb_offset += readNow;
  return readNow;
}

status_t BoxStream::Flush() { return this->inner->Flush(); }

void BoxStream::getPeerKey(unsigned char out[crypto_sign_PUBLICKEYBYTES]) {
  memcpy(out, this->peerkey, crypto_sign_PUBLICKEYBYTES);
}

BString BoxStream::cypherkey() {
  BString result("@");
  result << base64::encode(this->peerkey, crypto_sign_PUBLICKEYBYTES,
                           base64::STANDARD);
  result << ".ed25519";
  return result;
}
