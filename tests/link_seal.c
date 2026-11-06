#include "aervault_seal.h"
int main(void)
{
    av_seal s; av_context c={0}; uint8_t key[16]={0},prefix[4]={0},nonce[12],tag[16],out=0;
    (void)av_seal_init(&s,&c,1,key,prefix);
    return av_seal_encrypt(&s,0,0,"x",1,&out,nonce,tag,0)==AV_OK;
}
