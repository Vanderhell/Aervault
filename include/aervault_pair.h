#ifndef AERVAULT_PAIR_H
#define AERVAULT_PAIR_H
#include <stddef.h>
#include <stdint.h>
#define AV_PAIR_KEY_BYTES 32u
typedef struct { uint8_t tx_key[32]; uint8_t rx_key[32]; } av_pair_keys;
/* The application authenticates peer_identity (and, for X25519, computes the
 * checked shared secret using microdh) before calling. No transport is provided. */
int av_pair_derive(const void *secret, size_t secret_len, const void *salt,
                   size_t salt_len, const uint8_t local_identity[16],
                   const uint8_t peer_identity[16], int peer_verified,
                   int initiator, av_pair_keys *keys);
#endif
