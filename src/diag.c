#include "aervault_diag.h"
#include <string.h>
#define D_RECORD 0x6001u
static void dput32(uint8_t*p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8u*i));}
static void dput64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(8u*i));}
static uint32_t dget32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t dget64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(8u*i);return v;}
av_journal_status av_diag_init(av_diag*d,av_journal*j,size_t retain){if(!d||!j||!retain)return AV_JOURNAL_INVALID;d->journal=j;d->retain_count=retain;return AV_JOURNAL_OK;}
av_journal_status av_diag_write(av_diag*d,const av_diag_record*r,uint64_t*seq){uint8_t wire[46];if(!d||!d->journal||!r||r->length>AV_DIAG_PAYLOAD_MAX)return AV_JOURNAL_INVALID;memset(wire,0,sizeof wire);dput64(wire,r->timestamp);dput32(wire+8,r->event);wire[12]=r->severity;wire[13]=r->length;if(r->length)memcpy(wire+14,r->payload,r->length);return av_journal_append(d->journal,D_RECORD,wire,sizeof wire,seq);}
typedef struct{uint64_t target;av_diag_record*out;int found;} read_state;
static int read_visit(void*ctx,const av_journal_record*r,const uint8_t*p){read_state*s=(read_state*)ctx;if(r->type==D_RECORD&&r->sequence==s->target&&r->length==46&&p[13]<=AV_DIAG_PAYLOAD_MAX){s->out->timestamp=dget64(p);s->out->event=dget32(p+8);s->out->severity=p[12];s->out->length=p[13];memset(s->out->payload,0,sizeof s->out->payload);memcpy(s->out->payload,p+14,p[13]);s->found=1;}return 0;}
av_journal_status av_diag_read(av_diag*d,uint64_t seq,av_diag_record*out){read_state s;av_journal_status rc;if(!d||!out)return AV_JOURNAL_INVALID;s.target=seq;s.out=out;s.found=0;rc=av_journal_scan(d->journal,read_visit,&s);if(rc!=AV_JOURNAL_OK)return rc;return s.found?AV_JOURNAL_OK:AV_JOURNAL_FULL;}
typedef struct{size_t count,wanted,seen;uint64_t cutoff;int count_only;} diag_count;
static int count_diag(void*ctx,const av_journal_record*r,const uint8_t*p){diag_count*c=(diag_count*)ctx;(void)p;if(r->type==D_RECORD){c->count++;if(!c->count_only&&++c->seen==c->wanted)c->cutoff=r->sequence;}return 0;}
typedef struct{uint64_t cutoff;int trim;} diag_keep;
static int keep_diag(void*ctx,const av_journal_record*r,const uint8_t*p){diag_keep*k=(diag_keep*)ctx;(void)p;return r->type!=D_RECORD||!k->trim||r->sequence>=k->cutoff;}
av_journal_status av_diag_compact(av_diag*d){diag_count c;diag_keep k;av_journal_status rc;if(!d)return AV_JOURNAL_INVALID;memset(&c,0,sizeof c);c.count_only=1;rc=av_journal_scan(d->journal,count_diag,&c);if(rc!=AV_JOURNAL_OK&&rc!=AV_JOURNAL_TORN)return rc;k.cutoff=0;k.trim=0;if(c.count>d->retain_count){c.count_only=0;c.seen=0;c.wanted=c.count-d->retain_count+1;rc=av_journal_scan(d->journal,count_diag,&c);if(rc!=AV_JOURNAL_OK)return rc;k.cutoff=c.cutoff;k.trim=1;}return av_journal_compact(d->journal,keep_diag,&k);}
