#ifndef AERVAULT_SEAL_H
#define AERVAULT_SEAL_H
#include "aervault.h"
#include <stddef.h>
#include <stdint.h>
#define AV_SEAL_KEY_BYTES 16u
#define AV_SEAL_NONCE_BYTES 12u
#define AV_SEAL_TAG_BYTES 16u
typedef struct {
    av_context *vault;
    uint32_t counter_id;
    uint8_t key[AV_SEAL_KEY_BYTES];
    uint8_t nonce_prefix[4];
} av_seal;
/* Call once at setup. counter_id must be a provisioned AV_COUNTER object, and
 * its storage must be durable. This envelope is explicit; it does not migrate AV01. */
av_status av_seal_init(av_seal *, av_context *, uint32_t,
                       const uint8_t[AV_SEAL_KEY_BYTES], const uint8_t[4]);
/* Ciphertext is same length as plaintext. Any reservation failure leaves output untouched. */
av_status av_seal_encrypt(av_seal *, const void *, size_t, const void *, size_t,
                          void *, uint8_t[AV_SEAL_NONCE_BYTES], uint8_t[AV_SEAL_TAG_BYTES], av_error *);
av_status av_seal_decrypt(av_seal *, const void *, size_t, const void *, size_t,
                          const uint8_t[AV_SEAL_NONCE_BYTES], const uint8_t[AV_SEAL_TAG_BYTES], void *);
#endif
