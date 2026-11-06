#include "aervault.h"
#include "aervault_auth.h"
#include "aervault_seal.h"
#include "aervault_pair.h"
#include <stdint.h>
#include <string.h>

static av_context context;
static uint8_t workspace[2 * (AV_OVERHEAD + 16)];
static uint8_t key16[16] = {0};
#if defined(CONFIG_AV_FOOTPRINT_AUTH) && CONFIG_AV_FOOTPRINT_AUTH
static av_auth_key auth_key = { key16, sizeof key16 };
#endif
static uint8_t pair_private[32] = {1};
static uint8_t peer_public[32] = {9};
static uint8_t local_id[16] = {1}, peer_id[16] = {2};

static int verify_peer(void *ctx, const uint8_t id[16], const uint8_t pub[32])
{
    const uint8_t *pin = (const uint8_t *)ctx;
    return id[0] == peer_id[0] && memcmp(pub, pin, 32) == 0 ? 0 : -1;
}

void app_main(void)
{
    av_config cfg; av_error err; uint8_t value = 1;
    memset(&cfg, 0, sizeof cfg);
    cfg.work = workspace; cfg.work_size = sizeof workspace;
#if defined(CONFIG_AV_FOOTPRINT_AUTH) && CONFIG_AV_FOOTPRINT_AUTH
    { uint8_t tag[AV_TAG_BYTES]; cfg.tag = av_auth_tag; cfg.tag_ctx = &auth_key;
      (void)av_auth_tag(&auth_key, &value, sizeof value, tag); }
#endif
    (void)av_open(&context, &cfg);
    (void)av_put(&context, 1, &value, sizeof value, &err);
#if defined(CONFIG_AV_FOOTPRINT_SEAL) && CONFIG_AV_FOOTPRINT_SEAL
    { av_seal seal; uint8_t prefix[4]={0}, nonce[12], tag[16], ciphertext;
      (void)av_seal_init(&seal, &context, 1, key16, prefix);
      (void)av_seal_encrypt(&seal, 0, 0, &value, 1, &ciphertext, nonce, tag, &err); }
#endif
#if defined(CONFIG_AV_FOOTPRINT_PAIR) && CONFIG_AV_FOOTPRINT_PAIR && defined(AV_PAIR_HAS_X25519)
    { av_pair_keys keys;
      (void)av_pair_x25519(pair_private, local_id, peer_id, peer_public,
                           verify_peer, peer_public, 0, 0, 1, &keys); }
#endif
}
