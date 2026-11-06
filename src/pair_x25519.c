#include "aervault_pair.h"
#include "mcrypt.h"
#include "mdh.h"
#include <string.h>

int av_pair_x25519(const uint8_t local_private[32],const uint8_t local[16],
                   const uint8_t peer[16],const uint8_t peer_public[32],
                   av_pair_identity_verify_fn verify_peer,void *verify_ctx,
                   const void *salt,size_t salt_len,int initiator,av_pair_keys *keys)
{
    uint8_t shared[32]; int result;
    if(!keys) return -1;
    mcrypt_secure_clear(keys,sizeof *keys);
    if(!local_private||!local||!peer||!peer_public||!verify_peer||
       (!salt&&salt_len)||memcmp(local,peer,16)==0) return -1;
    if(verify_peer(verify_ctx,peer,peer_public)!=0) return -1;
    if(mdh_shared_secret_checked(shared,local_private,peer_public)!=MDH_OK) {
        mcrypt_secure_clear(keys,sizeof *keys); return -1;
    }
    result=av_pair_derive(shared,sizeof shared,salt,salt_len,local,peer,peer_public,
                          verify_peer,verify_ctx,initiator,keys);
    mdh_secret_clear(shared);
    return result;
}
