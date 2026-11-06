#include "aervault_history.h"
#include <string.h>
#define H_RECORD 0x3001u
static uint32_t hget32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void hput32(uint8_t*p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8u*i));}
static uint64_t hget64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(8u*i);return v;}
static void hput64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(8u*i));}
av_journal_status av_history_init(av_history*h,av_journal*j,uint8_t*w,size_t n,size_t retain){if(!h||!j||!w||n<12||!retain)return AV_JOURNAL_INVALID;h->journal=j;h->work=w;h->work_size=n;h->retain_count=retain;return AV_JOURNAL_OK;}
static av_journal_status core_status(av_status s){if(s==AV_OK)return AV_JOURNAL_OK;if(s==AV_IO)return AV_JOURNAL_IO;if(s==AV_LIMIT)return AV_JOURNAL_BOUNDS;if(s==AV_INVALID)return AV_JOURNAL_INVALID;return AV_JOURNAL_CORRUPT;}
av_journal_status av_history_checkpoint(av_history*h,av_context*c,uint32_t id,uint64_t*outseq){size_t n=0;uint64_t rev;av_journal_status s;av_status cs;av_error e;if(!h||!c||h->work_size<=12)return AV_JOURNAL_INVALID;cs=av_get(c,id,h->work+12,h->work_size-12,&n,&e);if(cs!=AV_OK)return core_status(cs);cs=av_get_revision(c,id,&rev,&e);if(cs!=AV_OK)return core_status(cs);hput32(h->work,id);hput64(h->work+4,rev);s=av_journal_append(h->journal,H_RECORD,h->work,n+12,outseq);return s;}
typedef struct{uint32_t id;uint64_t rev;void*out;size_t cap;size_t*len;int found;int small;} latest_state;
static int latest_visit(void*ctx,const av_journal_record*r,const uint8_t*p){latest_state*s=(latest_state*)ctx;if(r->type==H_RECORD&&r->length>=12&&hget32(p)==s->id&&(!s->found||hget64(p+4)>=s->rev)){s->rev=hget64(p+4);s->found=1;if(r->length-12>s->cap){s->small=1;return 0;}if(r->length>12)memcpy(s->out,p+12,r->length-12);*s->len=r->length-12;}return 0;}
av_journal_status av_history_latest(av_history*h,uint32_t id,uint64_t*rev,void*out,size_t cap,size_t*len){latest_state s;av_journal_status rc;if(!h||!rev||!len||(!out&&cap))return AV_JOURNAL_INVALID;memset(&s,0,sizeof s);s.id=id;s.out=out;s.cap=cap;s.len=len;rc=av_journal_scan(h->journal,latest_visit,&s);if(rc!=AV_JOURNAL_OK)return rc;if(s.small)return AV_JOURNAL_BOUNDS;if(!s.found)return AV_JOURNAL_FULL;*rev=s.rev;return AV_JOURNAL_OK;}
typedef struct{size_t count;uint64_t cutoff;size_t wanted;size_t seen;int count_only;} count_state;
static int count_visit(void*ctx,const av_journal_record*r,const uint8_t*p){count_state*s=(count_state*)ctx;(void)p;if(r->type==H_RECORD){s->count++;if(!s->count_only){s->seen++;if(s->seen==s->wanted)s->cutoff=r->sequence;}}return 0;}
typedef struct{uint64_t cutoff;int drop_history;} keep_state;
static int keep_history(void*ctx,const av_journal_record*r,const uint8_t*p){keep_state*s=(keep_state*)ctx;(void)p;if(r->type!=H_RECORD)return 1;return !s->drop_history||r->sequence>=s->cutoff;}
av_journal_status av_history_compact(av_history*h){count_state c;keep_state k;av_journal_status rc;if(!h)return AV_JOURNAL_INVALID;memset(&c,0,sizeof c);c.count_only=1;rc=av_journal_scan(h->journal,count_visit,&c);if(rc!=AV_JOURNAL_OK&&rc!=AV_JOURNAL_TORN)return rc;k.cutoff=0;k.drop_history=0;if(c.count>h->retain_count){c.count_only=0;c.wanted=c.count-h->retain_count+1;c.seen=0;rc=av_journal_scan(h->journal,count_visit,&c);if(rc!=AV_JOURNAL_OK)return rc;k.cutoff=c.cutoff;k.drop_history=1;}return av_journal_compact(h->journal,keep_history,&k);}
