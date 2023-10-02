#include "Connection.h"
#include "Base64.h"
#include <Errors.h>
#include <algorithm>
#include <cstring>
#include <support/ByteOrder.h>
#include <utility>

const unsigned char SSB_NETWORK_ID[32] = {
    0xd4, 0xa1, 0xcb, 0x88, 0xa6, 0x6f, 0x02, 0xf8, 0xdb, 0x63, 0x5c,
    0xe2, 0x64, 0x41, 0xcc, 0x5d, 0xac, 0x1b, 0x08, 0x42, 0x0c, 0xea,
    0xac, 0x23, 0x08, 0x39, 0xb7, 0x55, 0x84, 0x5a, 0x9f, 0xfb};

// Client side of handshake (server public key known)
BoxStream::BoxStream(std::unique_ptr<BDataIO> inner,
                     const unsigned char netkey[crypto_auth_KEYBYTES],
                     Ed25519Secret *myId,
                     const unsigned char srvkey[crypto_sign_PUBLICKEYBYTES]) {
  unsigned char *seckey = myId->secret;
  unsigned char *pubkey = myId->pubkey;
  this->inner = std::move(inner);
  memcpy(this->peerkey, srvkey, crypto_sign_PUBLICKEYBYTES);
  // Section 1: Client Hello
  unsigned char e_pubkey[crypto_box_PUBLICKEYBYTES];
  unsigned char e_seckey[crypto_box_SECRETKEYBYTES];
  if (crypto_box_keypair(e_pubkey, e_seckey) < 0)
    throw KEYGEN_FAIL;
  unsigned char server_e_key[32];
  {
    unsigned char hmac[32];
    if (crypto_auth(hmac, e_pubkey, 32, netkey) < 0)
      throw HMAC_FAIL;
    memcpy(this->recvnonce, hmac, 24);
    unsigned char buf[64];
    memcpy(buf, hmac, 32);
    memcpy(buf + 32, e_pubkey, 32);
    if (this->inner->WriteExactly(buf, 64, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    // Section 2: Server hello
    if (this->inner->ReadExactly(buf, 64, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    if (crypto_auth_verify(buf, buf + 32, 32, netkey) != 0)
      throw WRONG_NETKEY;
    memcpy(server_e_key, buf + 32, 32);
  }
  unsigned char secret1[crypto_scalarmult_BYTES]; // Shared secret ab
  if (crypto_scalarmult(secret1, e_seckey, server_e_key) < 0)
    throw SECRET_FAILED;
  unsigned char secret2[crypto_scalarmult_BYTES]; // Shared secret aB
  {
    unsigned char remote_curve[32];
    if (crypto_sign_ed25519_pk_to_curve25519(remote_curve, srvkey) < 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(secret2, e_seckey, remote_curve) < 0)
      throw SECRET_FAILED;
  }
  // Section 3: Client authenticate
  unsigned char detached_signature_a[crypto_sign_BYTES];
  {
    unsigned char sgnmsg[64 + crypto_hash_sha256_BYTES];
    memcpy(sgnmsg, netkey, 32);
    memcpy(sgnmsg + 32, srvkey, 32);
    unsigned long long siglen = 32;
    if (crypto_hash_sha256(sgnmsg + 64, secret1, 32) != 0)
      throw SECRET_FAILED;
    if (crypto_sign_detached(detached_signature_a, &siglen, sgnmsg,
                             sizeof(sgnmsg), seckey) != 0)
      throw SECRET_FAILED;
    unsigned char boxkey[crypto_hash_sha256_BYTES];
    {
      unsigned char keyparts[32 * 3];
      memcpy(keyparts, netkey, 32);
      memcpy(keyparts + 32, secret1, 32);
      memcpy(keyparts + 64, secret2, 32);
      if (crypto_hash_sha256(boxkey, keyparts, 32 * 3) != 0)
        throw SECRET_FAILED;
    }
    unsigned char msg[64 + 32];
    unsigned char secretbox[crypto_secretbox_MACBYTES + 64 + 32];
    unsigned char zerobytes[24];
    memset(zerobytes, 0, 24);
    memcpy(msg, detached_signature_a, 32);
    memcpy(msg + 64, pubkey, 32);
    if (crypto_secretbox_easy(secretbox, msg, 64 + 32, zerobytes, boxkey) != 0)
      throw SECRET_FAILED;
    if (this->inner->WriteExactly(secretbox, 112, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
  }
  unsigned char secret3[crypto_scalarmult_BYTES]; // Shared secret Ab
  {
    unsigned char curvified[32];
    if (crypto_sign_ed25519_sk_to_curve25519(curvified, seckey) != 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(secret3, curvified, server_e_key) != 0)
      throw SECRET_FAILED;
  }
  // Section 4: Server Accept
  {
    unsigned char secretbox[80];
    if (this->inner->ReadExactly(secretbox, 80, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    unsigned char detached_signature_b[80 - crypto_secretbox_MACBYTES];
    unsigned char key[crypto_hash_sha256_BYTES];
    unsigned char seed[32 * 5];
    unsigned char nonce[24];
    memcpy(seed, netkey, 32);
    memcpy(seed + 32, secret1, 32);
    memcpy(seed + 64, secret2, 32);
    memcpy(seed + 96, secret3, 32);
    memset(nonce, 0, 24);
    if (crypto_hash_sha256(key, seed, 32 * 4) != 0)
      throw SECRET_FAILED;
    if (crypto_secretbox_open_easy(detached_signature_b, secretbox, 80, nonce,
                                   key) != 0)
      throw SECRET_FAILED;
    unsigned char seed2[64];
    if (crypto_hash_sha256(seed2, key, 32) != 0)
      throw SECRET_FAILED;
    memcpy(seed2 + 32, srvkey, 32);
    if (crypto_hash_sha256(this->sendkey, seed2, 64) != 0)
      throw SECRET_FAILED;
    memcpy(seed2 + 32, pubkey, 32);
    if (crypto_hash_sha256(this->recvkey, seed2, 64) != 0)
      throw SECRET_FAILED;
    memcpy(seed + 32, detached_signature_a, crypto_sign_BYTES);
    memcpy(seed + 32 + crypto_sign_BYTES, pubkey, 32);
    if (crypto_hash_sha256(seed + 32 * 2 + crypto_sign_BYTES, secret1, 32) != 0)
      throw SECRET_FAILED;
    if (crypto_sign_verify_detached(detached_signature_b, seed,
                                    32 * 3 + crypto_sign_BYTES, srvkey) != 0)
      throw SECRET_FAILED;
  }
}

// Server side of handshake (will receive client public key)
BoxStream::BoxStream(std::unique_ptr<BDataIO> inner,
                     const unsigned char netkey[crypto_auth_KEYBYTES],
                     Ed25519Secret *myId) {
  unsigned char *seckey = myId->secret;
  unsigned char *pubkey = myId->pubkey;
  this->inner = std::move(inner);
  unsigned char client_e_key[crypto_box_PUBLICKEYBYTES];
  // Section 1: Client hello
#define BUFFER_LENGTH (crypto_auth_BYTES + crypto_box_PUBLICKEYBYTES)
  {
    unsigned char buf[BUFFER_LENGTH];
    for (int i = 0; i < BUFFER_LENGTH; i++)
      buf[i] = 12;
    if (this->inner->ReadExactly(buf, BUFFER_LENGTH) != B_OK)
      throw HANDSHAKE_HANGUP;
    if (crypto_auth_verify(buf, buf + crypto_auth_BYTES,
                           crypto_box_PUBLICKEYBYTES, netkey) != 0)
      throw WRONG_NETKEY;
    memcpy(client_e_key, buf + crypto_auth_BYTES, crypto_box_PUBLICKEYBYTES);
    memcpy(this->sendnonce, buf, crypto_secretbox_NONCEBYTES);
  }
  // Section 2: Server hello
  unsigned char e_pubkey[crypto_box_PUBLICKEYBYTES];
  unsigned char e_seckey[crypto_box_SECRETKEYBYTES];
  if (crypto_box_keypair(e_pubkey, e_seckey) < 0)
    throw KEYGEN_FAIL;
  {
    unsigned char buf[BUFFER_LENGTH];
    memcpy(buf + crypto_auth_BYTES, e_pubkey, crypto_box_PUBLICKEYBYTES);
    if (crypto_auth(buf, e_pubkey, crypto_box_PUBLICKEYBYTES, netkey) != 0)
      throw HMAC_FAIL;
    if (this->inner->WriteExactly(buf, BUFFER_LENGTH, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    memcpy(this->sendnonce, buf, crypto_secretbox_NONCEBYTES);
  }
#undef BUFFER_LENGTH
  unsigned char secret1[crypto_scalarmult_BYTES]; // Shared secret ab
  unsigned char secret2[crypto_scalarmult_BYTES]; // Shared secret aB
  {
    if (crypto_scalarmult(secret1, e_seckey, client_e_key) < 0)
      throw SECRET_FAILED;
    unsigned char remote_curve[crypto_scalarmult_curve25519_BYTES];
    if (crypto_sign_ed25519_sk_to_curve25519(remote_curve, seckey) < 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(secret2, remote_curve, client_e_key) < 0)
      throw SECRET_FAILED;
  }
  // Section 3: Client authenticate
  unsigned char secret3[crypto_scalarmult_BYTES]; // Shared secret Ab
  unsigned char detached_signature_a[crypto_sign_BYTES];
  {

    unsigned char buf[112];
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    unsigned char keyparts[crypto_scalarmult_BYTES * 3];
    unsigned char key[crypto_hash_sha256_BYTES];
    if (this->inner->ReadExactly(buf, 112, NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    memset(nonce, 0, crypto_secretbox_NONCEBYTES);
    memcpy(keyparts, netkey, crypto_auth_KEYBYTES);
    memcpy(keyparts + crypto_auth_KEYBYTES, secret1, crypto_scalarmult_BYTES);
#define OFFSET_2 (crypto_auth_KEYBYTES + crypto_scalarmult_BYTES)
    memcpy(keyparts + OFFSET_2, secret2, crypto_scalarmult_BYTES);
    if (crypto_hash_sha256(key, keyparts, sizeof(keyparts)) != 0)
      throw SECRET_FAILED;
#undef OFFSET_2
    if (crypto_secretbox_open_easy(buf + crypto_secretbox_MACBYTES, buf, 112,
                                   nonce, key) != 0)
      throw SECRET_FAILED;
    memcpy(this->peerkey, buf + crypto_secretbox_MACBYTES + crypto_sign_BYTES,
           crypto_auth_KEYBYTES);
    memcpy(keyparts + crypto_auth_KEYBYTES, pubkey, crypto_sign_PUBLICKEYBYTES);
#define OFFSET_2 (crypto_auth_KEYBYTES + crypto_sign_PUBLICKEYBYTES)
    if (crypto_hash_sha256(keyparts + OFFSET_2, secret1, sizeof(secret1)) != 0)
      throw SECRET_FAILED;
#undef OFFSET_2
#define MSGLEN                                                                 \
  (crypto_auth_KEYBYTES + crypto_sign_PUBLICKEYBYTES + crypto_hash_sha256_BYTES)
    if (crypto_sign_verify_detached(buf + crypto_secretbox_MACBYTES, keyparts,
                                    MSGLEN, this->peerkey) != 0)
      throw SECRET_FAILED;
#undef MSGLEN
    if (crypto_sign_ed25519_pk_to_curve25519(key, this->peerkey) != 0)
      throw SECRET_FAILED;
    if (crypto_scalarmult(secret3, e_seckey, key) != 0)
      throw SECRET_FAILED;
    memcpy(detached_signature_a, buf + crypto_secretbox_MACBYTES,
           crypto_sign_BYTES);
#undef OFFSET_2
  }
  // Section 4: Server Accept
  {
    unsigned char seed[crypto_auth_KEYBYTES + crypto_sign_BYTES +
                       crypto_sign_PUBLICKEYBYTES + crypto_hash_sha256_BYTES];
    memcpy(seed, netkey, crypto_auth_KEYBYTES);
    memcpy(seed + 32, detached_signature_a, crypto_sign_BYTES);
    memcpy(seed + 32 + crypto_sign_BYTES, this->peerkey,
           crypto_sign_PUBLICKEYBYTES);
    if (crypto_hash_sha256(seed + 32 + crypto_sign_BYTES +
                               crypto_sign_PUBLICKEYBYTES,
                           secret1, crypto_scalarmult_BYTES) != 0)
      throw SECRET_FAILED;
    unsigned char msg[crypto_secretbox_MACBYTES + crypto_sign_BYTES];
    if (crypto_sign_detached(msg + crypto_secretbox_MACBYTES, NULL, seed,
                             32 + crypto_sign_BYTES +
                                 crypto_sign_PUBLICKEYBYTES +
                                 crypto_hash_sha256_BYTES,
                             pubkey) != 0)
      throw SECRET_FAILED;
    memcpy(seed + 32, secret1, crypto_scalarmult_BYTES);
    memcpy(seed + 32 + crypto_scalarmult_BYTES, secret2,
           crypto_scalarmult_BYTES);
    memcpy(seed + 32 + crypto_scalarmult_BYTES * 2, secret3,
           crypto_scalarmult_BYTES);
    unsigned char key[crypto_hash_sha256_BYTES];
    if (crypto_hash_sha256(key, seed, sizeof(seed)) != 0)
      throw SECRET_FAILED;
    unsigned char nonce[crypto_box_NONCEBYTES];
    memset(nonce, 0, crypto_box_NONCEBYTES);
    if (crypto_secretbox_easy(msg, msg + crypto_secretbox_MACBYTES,
                              crypto_sign_BYTES, nonce, key) != 0)
      throw SECRET_FAILED;
    if (this->inner->WriteExactly(msg, sizeof(msg), NULL) != B_OK)
      throw HANDSHAKE_HANGUP;
    if (crypto_hash_sha256(seed, key, crypto_hash_sha256_BYTES) != 0)
      throw HANDSHAKE_HANGUP;
    memcpy(seed + crypto_hash_sha256_BYTES, client_e_key,
           crypto_box_PUBLICKEYBYTES);
    if (crypto_hash_sha256(this->sendkey, seed,
                           crypto_hash_sha256_BYTES +
                               crypto_secretbox_KEYBYTES) != 0)
      throw SECRET_FAILED;
    memcpy(seed + crypto_hash_sha256_BYTES, e_pubkey,
           crypto_box_PUBLICKEYBYTES);
    if (crypto_hash_sha256(this->recvkey, seed,
                           crypto_hash_sha256_BYTES +
                               crypto_secretbox_KEYBYTES) != 0)
      throw SECRET_FAILED;
  }
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
    if (this->inner->ReadExactly(header, 34) != B_OK) {
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
