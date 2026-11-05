#ifndef AERVAULT_AUTH_H
#define AERVAULT_AUTH_H
#include "aervault.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { const uint8_t *key; size_t key_len; } av_auth_key;
/* Output is the AV01-compatible leftmost 16 bytes of HMAC-SHA256. */
int av_auth_tag(void *key_ctx, const uint8_t *bytes, size_t length,
                uint8_t tag[AV_TAG_BYTES]);
#endif
