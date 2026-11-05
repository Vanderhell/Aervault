#include "aervault_seal.h"
#include "mcrypt.h"
av_status av_seal_init(av_seal *s, av_context *v, uint32_t id,
                       const uint8_t key[16], const uint8_t prefix[4])
{
    unsigned i;
    if (!s || !v || !id || !key || !prefix) return AV_INVALID;
    s->vault = v; s->counter_id = id;
    for (i=0;i<16;i++) s->key[i]=key[i];
    for (i=0;i<4;i++) s->nonce_prefix[i]=prefix[i];
    return AV_OK;
}
av_status av_seal_encrypt(av_seal *s, const void *aad, size_t an,
                          const void *plain, size_t n, void *cipher,
                          uint8_t nonce[12], uint8_t tag[16], av_error *e)
{
    uint64_t seq; unsigned i; mcrypt_aes128_t aes; uint8_t local_nonce[12];
    av_status st;
    if (!s || !s->vault || (!plain && n) || (!cipher && n) || (!aad && an) || !nonce || !tag) return AV_INVALID;
    st=av_counter_next(s->vault,s->counter_id,&seq,e);
    if (st!=AV_OK) return st; /* uncertain reservation never encrypts */
    for(i=0;i<4;i++) local_nonce[i]=s->nonce_prefix[i];
    for(i=0;i<8;i++) local_nonce[4+i]=(uint8_t)(seq>>(56-8*i));
    if(mcrypt_aes128_init(&aes,s->key)!=MCRYPT_OK) return AV_IO;
    if(mcrypt_aes128_gcm_encrypt(&aes,local_nonce,aad,an,plain,n,cipher,tag)!=MCRYPT_OK) {
        mcrypt_secure_clear(&aes,sizeof aes); return AV_IO;
    }
    mcrypt_secure_clear(&aes,sizeof aes);
    for(i=0;i<12;i++) nonce[i]=local_nonce[i];
    mcrypt_secure_clear(local_nonce,sizeof local_nonce); return AV_OK;
}
av_status av_seal_decrypt(av_seal *s, const void *aad,size_t an,const void *cipher,size_t n,
                          const uint8_t nonce[12],const uint8_t tag[16],void *plain)
{
    mcrypt_aes128_t aes; mcrypt_status_t m;
    if(!s||(!aad&&an)||(!cipher&&n)||(!plain&&n)||!nonce||!tag) return AV_INVALID;
    if(mcrypt_aes128_init(&aes,s->key)!=MCRYPT_OK) return AV_IO;
    m=mcrypt_aes128_gcm_decrypt(&aes,nonce,aad,an,cipher,n,tag,plain);
    mcrypt_secure_clear(&aes,sizeof aes);
    if(m!=MCRYPT_OK) { if(plain&&n) mcrypt_secure_clear(plain,n); return AV_AUTH_ERROR; }
    return AV_OK;
}
