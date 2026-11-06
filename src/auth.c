#include "aervault_auth.h"
#include "mcrypt.h"
int av_auth_tag(void *opaque, const uint8_t *bytes, size_t length,
                uint8_t tag[AV_TAG_BYTES])
{
    av_auth_key *key = (av_auth_key *)opaque;
    uint8_t full[MCRYPT_HMAC_SHA256_SIZE];
    if (!key || !key->key || !key->key_len || (!bytes && length) || !tag) return -1;
    if (mcrypt_hmac_sha256(key->key, key->key_len, bytes, length, full) != MCRYPT_OK) {
        mcrypt_secure_clear(full, sizeof full);
        return -1;
    }
    { size_t i; for (i = 0; i < AV_TAG_BYTES; ++i) tag[i] = full[i]; }
    mcrypt_secure_clear(full, sizeof full);
    return 0;
}
