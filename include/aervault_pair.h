#ifndef AERVAULT_PAIR_H
#define AERVAULT_PAIR_H
#include <stddef.h>
#include <stdint.h>
#define AV_PAIR_KEY_BYTES 32u
typedef struct { uint8_t tx_key[32]; uint8_t rx_key[32]; } av_pair_keys;
typedef int (*av_pair_identity_verify_fn)(void *, const uint8_t peer_identity[16],
                                          const uint8_t peer_public_key[32]);
/* Verifier must authenticate the exact identity/public-key pair. For X25519,
 * secret is mdh_shared_secret_checked() output; PSK mode passes the pair key.
 * Application owns trust policy, handshake transcript and transport. */
int av_pair_derive(const void *secret, size_t secret_len, const void *salt,
                   size_t salt_len, const uint8_t local_identity[16],
                   const uint8_t peer_identity[16], const uint8_t peer_public_key[32],
                   av_pair_identity_verify_fn verify_peer, void *verify_ctx,
                   int initiator, av_pair_keys *keys);
#if defined(AV_PAIR_HAS_X25519)
/* Direct checked-X25519 adapter. The verifier binds peer_identity to the exact
 * peer_public_key before microdh derives a secret or keys are released. */
int av_pair_x25519(const uint8_t local_private_key[32],
                   const uint8_t local_identity[16],
                   const uint8_t peer_identity[16],
                   const uint8_t peer_public_key[32],
                   av_pair_identity_verify_fn verify_peer, void *verify_ctx,
                   const void *salt, size_t salt_len, int initiator,
                   av_pair_keys *keys);
#endif
#endif
