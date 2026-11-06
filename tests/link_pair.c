#include "aervault_pair.h"
static int trust(void *ctx,const uint8_t id[16],const uint8_t pub[32])
{ (void)ctx; (void)id; (void)pub; return 0; }
int main(void)
{
#if defined(AV_PAIR_HAS_X25519)
    uint8_t priv[32]={1},local[16]={1},peer[16]={2},pub[32]={9}; av_pair_keys keys;
    return av_pair_x25519(priv,local,peer,pub,trust,0,0,0,1,&keys)==123;
#else
    uint8_t secret[32]={1},local[16]={1},peer[16]={2},pub[32]={9}; av_pair_keys keys;
    return av_pair_derive(secret,sizeof secret,0,0,local,peer,pub,trust,0,1,&keys)==123;
#endif
}
