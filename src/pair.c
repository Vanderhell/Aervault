#include "aervault_pair.h"
#include "mcrypt.h"
#include <string.h>
static int hmac(const uint8_t *k,size_t kn,const void *d,size_t n,uint8_t out[32])
{ return mcrypt_hmac_sha256(k,kn,d,n,out)==MCRYPT_OK?0:-1; }
/* RFC5869 HKDF-SHA256, using only microcrypt's public HMAC primitive. */
static int hkdf(const uint8_t *ikm,size_t in,const void *salt,size_t sn,
                const uint8_t *info,size_t fn,uint8_t out[32])
{
    uint8_t zero[32]={0},prk[32],buf[128]; size_t i;
    if(sn>32 || fn>95) return -1;
    if(hmac(sn?(const uint8_t *)salt:zero,sn?sn:32,ikm,in,prk)) return -1;
    memcpy(buf,info,fn); buf[fn]=1;
    i=fn+1;
    if(hmac(prk,32,buf,i,out)) { mcrypt_secure_clear(prk,32); return -1; }
    mcrypt_secure_clear(prk,32); mcrypt_secure_clear(buf,sizeof buf); return 0;
}
int av_pair_derive(const void *secret,size_t secret_len,const void *salt,size_t salt_len,
                   const uint8_t local[16],const uint8_t peer[16],int verified,
                   int initiator,av_pair_keys *keys)
{
    static const uint8_t domain[]="Aervault MCU pair v1";
    uint8_t info[64]; size_t p=0;
    if(!secret||!secret_len||!local||!peer||!keys||!verified||(!salt&&salt_len)) return -1;
    if(memcmp(local,peer,16)==0) return -1;
    memcpy(info+p,domain,sizeof domain-1); p+=sizeof domain-1;
    if(initiator) { memcpy(info+p,local,16); memcpy(info+p+16,peer,16); }
    else { memcpy(info+p,peer,16); memcpy(info+p+16,local,16); }
    p+=32; info[p++]=0;
    if(hkdf((const uint8_t *)secret,secret_len,salt,salt_len,info,p,keys->tx_key)) {
        mcrypt_secure_clear(keys,sizeof *keys); return -1;
    }
    info[p-1]=1;
    if(hkdf((const uint8_t *)secret,secret_len,salt,salt_len,info,p,keys->rx_key)) {
        mcrypt_secure_clear(keys,sizeof *keys); return -1;
    }
    if(!initiator) { uint8_t t[32]; memcpy(t,keys->tx_key,32); memcpy(keys->tx_key,keys->rx_key,32); memcpy(keys->rx_key,t,32); mcrypt_secure_clear(t,32); }
    mcrypt_secure_clear(info,sizeof info);
    return 0;
}
