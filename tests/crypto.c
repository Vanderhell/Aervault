#include "aervault_auth.h"
#include "aervault_pair.h"
#include "aervault_seal.h"
#include "mcrypt.h"
#if defined(AV_PAIR_HAS_X25519)
#include "mdh.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T(x) do { if (!(x)) { fprintf(stderr,"crypto test line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i=0;i<n;i++) {
        unsigned x;
        if (sscanf(s+i*2,"%2x",&x)!=1) return 0;
        out[i]=(uint8_t)x;
    }
    return s[n*2]=='\0';
}

typedef struct {
    uint8_t slot[2][256]; size_t len[2]; int fail_durable;
} media;
static int rd(void *p,uint32_t id,unsigned slot,uint8_t *out,size_t cap,size_t *n)
{
    media *m=(media *)p; (void)id;
    if(!m->len[slot]) return 1;
    if(m->len[slot]>cap) return -2;
    memcpy(out,m->slot[slot],m->len[slot]); *n=m->len[slot]; return 0;
}
static int wr(void *p,uint32_t id,unsigned slot,const uint8_t *in,size_t n)
{
    media *m=(media *)p; (void)id;
    memcpy(m->slot[slot],in,n); m->len[slot]=n;
    if(m->fail_durable) { m->fail_durable=0; return -77; }
    return 0;
}
static int valid(void *p,uint32_t id) { (void)p; return id==9; }
static int verify_peer(void *p,const uint8_t id[16],const uint8_t pub[32])
{
    uint8_t *expected=(uint8_t *)p;
    return id[0]==expected[0] && memcmp(pub,expected+16,32)==0 ? 0 : -1;
}
static void check_auth(void)
{
    uint8_t keybytes[20], expected[16], got[16]; size_t i;
    av_auth_key key;
    for(i=0;i<sizeof keybytes;i++) keybytes[i]=0x0b;
    T(unhex("b0344c61d8db38535ca8afceaf0bf12b",expected,16));
    key.key=keybytes; key.key_len=sizeof keybytes;
    T(av_auth_tag(&key,(const uint8_t *)"Hi There",8,got)==0);
    T(memcmp(got,expected,16)==0);
}
static void check_gcm_and_seal_decrypt(void)
{
    uint8_t key[16]={0},nonce[12]={0},plain[16]={0},cipher[16],tag[16];
    uint8_t want_cipher[16],want_tag[16],out[16],badkey[16]={1};
    mcrypt_aes128_t aes; av_seal s; av_context dummy;
    T(unhex("0388dace60b6a392f328c2b971b2fe78",want_cipher,16));
    T(unhex("ab6e47d42cec13bdf53a67b21257bddf",want_tag,16));
    T(mcrypt_aes128_init(&aes,key)==MCRYPT_OK);
    T(mcrypt_aes128_gcm_encrypt(&aes,nonce,NULL,0,plain,16,cipher,tag)==MCRYPT_OK);
    T(memcmp(cipher,want_cipher,16)==0 && memcmp(tag,want_tag,16)==0);
    memset(&s,0,sizeof s); memset(&dummy,0,sizeof dummy);
    T(av_seal_init(&s,&dummy,1,key,(const uint8_t[4]){0,0,0,0})==AV_OK);
    memset(out,0xa5,sizeof out);
    T(av_seal_decrypt(&s,NULL,0,cipher,16,nonce,tag,out)==AV_OK);
    T(memcmp(out,plain,16)==0);
    tag[0]^=1; memset(out,0xa5,sizeof out);
    T(av_seal_decrypt(&s,NULL,0,cipher,16,nonce,tag,out)==AV_AUTH_ERROR);
    { size_t i; for(i=0;i<sizeof out;i++) T(out[i]==0); }
    tag[0]^=1; cipher[0]^=1; memset(out,0xa5,sizeof out);
    T(av_seal_decrypt(&s,NULL,0,cipher,16,nonce,tag,out)==AV_AUTH_ERROR);
    { size_t i; for(i=0;i<sizeof out;i++) T(out[i]==0); }
    T(mcrypt_aes128_init(&aes,badkey)==MCRYPT_OK);
    memset(out,0xa5,sizeof out);
    T(mcrypt_aes128_gcm_decrypt(&aes,nonce,NULL,0,want_cipher,16,want_tag,out)==MCRYPT_ERR_AUTH_FAILED);
    T(av_seal_init(&s,&dummy,1,badkey,(const uint8_t[4]){0,0,0,0})==AV_OK);
    memset(out,0xa5,sizeof out);
    T(av_seal_decrypt(&s,NULL,0,want_cipher,16,nonce,want_tag,out)==AV_AUTH_ERROR);
    { size_t i; for(i=0;i<sizeof out;i++) T(out[i]==0); }
}
static void check_pair(void)
{
    uint8_t secret[32]={7},pub_a[32]={1},pub_b[32]={2},a[16]={1},b[16]={2};
    uint8_t pin_a[48]={1},pin_b[48]={2};
    uint8_t want_tx[32],want_rx[32];
    av_pair_keys ka,kb;
    T(unhex("96ef04690b76e75899403370719d7a3d9c67c1ebfaee37ad202fb4adaa13bc8a",want_tx,32));
    T(unhex("3d16fdf7c8355eab5c8841bbd5a6d80899ae95cf2f7185ff9d91e9220d144a8e",want_rx,32));
    memcpy(pin_a+16,pub_a,32); memcpy(pin_b+16,pub_b,32);
    T(av_pair_derive(secret,32,NULL,0,a,b,pub_b,verify_peer,pin_b,1,&ka)==0);
    T(av_pair_derive(secret,32,NULL,0,b,a,pub_a,verify_peer,pin_a,0,&kb)==0);
    T(memcmp(ka.tx_key,want_tx,32)==0 && memcmp(ka.rx_key,want_rx,32)==0);
    T(memcmp(ka.tx_key,kb.rx_key,32)==0 && memcmp(ka.rx_key,kb.tx_key,32)==0);
    T(memcmp(ka.tx_key,ka.rx_key,32)!=0);
    memset(&ka,0xa5,sizeof ka);
    T(av_pair_derive(secret,32,NULL,0,a,b,pub_b,NULL,pin_b,1,&ka)!=0);
    { size_t i; const uint8_t *q=(const uint8_t *)&ka; for(i=0;i<sizeof ka;i++) T(q[i]==0); }
    pub_b[0]^=1;
    T(av_pair_derive(secret,32,NULL,0,a,b,pub_b,verify_peer,pin_b,1,&ka)!=0);
}
#if defined(AV_PAIR_HAS_X25519)
static void check_x25519(void)
{
    uint8_t alice_sk[32],bob_sk[32],alice_pk[32],bob_pk[32],shared_a[32],shared_b[32],want[32];
    uint8_t alice_id[16]={1},bob_id[16]={2},pin_alice[48]={1},pin_bob[48]={2};
    av_pair_keys a,b;
    T(unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",alice_sk,32));
    T(unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",bob_sk,32));
    T(unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",alice_pk,32));
    T(unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",bob_pk,32));
    T(unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",want,32));
    memcpy(pin_alice+16,alice_pk,32); memcpy(pin_bob+16,bob_pk,32);
    T(mdh_shared_secret_checked(shared_a,alice_sk,bob_pk)==MDH_OK);
    T(mdh_shared_secret_checked(shared_b,bob_sk,alice_pk)==MDH_OK);
    T(memcmp(shared_a,want,32)==0 && memcmp(shared_a,shared_b,32)==0);
    T(av_pair_x25519(alice_sk,alice_id,bob_id,bob_pk,verify_peer,pin_bob,NULL,0,1,&a)==0);
    T(av_pair_x25519(bob_sk,bob_id,alice_id,alice_pk,verify_peer,pin_alice,NULL,0,0,&b)==0);
    T(memcmp(a.tx_key,b.rx_key,32)==0 && memcmp(a.rx_key,b.tx_key,32)==0);
    bob_pk[0]^=1;
    T(av_pair_x25519(alice_sk,alice_id,bob_id,bob_pk,verify_peer,pin_bob,NULL,0,1,&a)!=0);
    mdh_secret_clear(shared_a); mdh_secret_clear(shared_b);
}
#endif
static void check_nonce_recovery(void)
{
    media m={{ {0} },{0},0}; uint8_t workspace[120],reboot_work[120],storagekey[16]={9},prefix[4]={3,4,5,6};
    uint8_t cipher1[1],cipher2[1],nonce1[12],nonce2[12],tag1[16],tag2[16],sentinel=0x5a;
    av_backend backend={&m,rd,wr,valid};
    av_object_rule rule={9,{9},1,AV_COUNTER,8};
    av_auth_key tagkey={storagekey,sizeof storagekey};
    av_config cfg={&backend,&rule,1,workspace,sizeof workspace,av_auth_tag,&tagkey};
    av_context c,reboot; av_seal seal; av_error err;
    T(av_open(&c,&cfg)==AV_OK);
    T(av_provision_id(&c,9,NULL,0,&err)==AV_OK);
    T(av_seal_init(&seal,&c,9,storagekey,prefix)==AV_OK);
    T(av_seal_encrypt(&seal,NULL,0,"a",1,cipher1,nonce1,tag1,&err)==AV_OK);
    m.fail_durable=1;
    T(av_seal_encrypt(&seal,NULL,0,"b",1,&sentinel,nonce2,tag2,&err)==AV_UNCERTAIN);
    T(sentinel==0x5a);
    memset(reboot_work,0xa5,sizeof reboot_work);
    cfg.work=reboot_work; T(av_open(&reboot,&cfg)==AV_OK);
    T(av_seal_init(&seal,&reboot,9,storagekey,prefix)==AV_OK);
    T(av_seal_encrypt(&seal,NULL,0,"c",1,cipher2,nonce2,tag2,&err)==AV_OK);
    T(memcmp(nonce1,nonce2,12)!=0);
    T(nonce1[11]==1 && nonce2[11]==3);
}
int main(void)
{
    check_auth(); check_gcm_and_seal_decrypt(); check_pair();
#if defined(AV_PAIR_HAS_X25519)
    check_x25519();
#endif
    check_nonce_recovery();
    puts("crypto adapters: PASS"); return 0;
}
