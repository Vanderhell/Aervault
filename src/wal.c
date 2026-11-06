#include "aervault_wal.h"
#include <string.h>

#define WAL_PREPARE 0x1001u
#define WAL_FINISH  0x1002u
#define WAL_PREPARE_BATCH 0x1003u

static void w32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8u*i));}
static void w64(uint8_t *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(8u*i));}
static uint32_t r32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t r64(const uint8_t *p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(8u*i);return v;}

static int walk_changes(av_wal *w,const uint8_t *payload,size_t length,int use_new,int legacy)
{
    size_t pos=legacy?0u:4u,i;
    uint32_t count=legacy?1u:0u;
    if(length<(legacy?12u:4u))return -1;
    if(!legacy)count=r32(payload);
    for(i=0;i<(size_t)count;i++){
        uint32_t key,old_len,new_len;
        size_t data_pos;
        const uint8_t *value;
        size_t value_len;
        if(pos>length||length-pos<12u)return -1;
        key=r32(payload+pos);old_len=r32(payload+pos+4u);new_len=r32(payload+pos+8u);pos+=12u;
        if((size_t)old_len>length-pos||(size_t)new_len>length-pos-(size_t)old_len)return -1;
        data_pos=pos;
        value=payload+data_pos+(use_new?(size_t)old_len:0u);
        value_len=use_new?(size_t)new_len:(size_t)old_len;
        if(w->apply(w->apply_ctx,key,value,value_len)!=0)return -2;
        pos+=(size_t)old_len+(size_t)new_len;
    }
    return pos==length?0:-1;
}

av_journal_status av_wal_init(av_wal *w,av_journal *j,uint8_t *buf,size_t n,av_wal_apply_fn fn,void *ctx)
{
    if(!w||!j||!buf||n<16u||!fn)return AV_JOURNAL_INVALID;
    w->journal=j;w->work=buf;w->work_size=n;w->apply=fn;w->apply_ctx=ctx;w->recovered=0;
    return AV_JOURNAL_OK;
}

int av_wal_is_ready(const av_wal *w){return w&&w->journal&&w->recovered;}

av_journal_status av_wal_read(av_wal *w,av_wal_read_fn read_fn,uint32_t key,void *out,size_t cap,size_t *length)
{
    if(!w||!read_fn||!length||(!out&&cap))return AV_JOURNAL_INVALID;
    if(!w->recovered)return AV_JOURNAL_NOT_READY;
    return read_fn(w->apply_ctx,key,out,cap,length)==0?AV_JOURNAL_OK:AV_JOURNAL_IO;
}

av_journal_status av_wal_update_many(av_wal *w,const av_wal_change *changes,size_t count)
{
    size_t total=4u,i,pos;uint64_t prepare_seq;uint8_t finish[9];av_journal_status rc;
    if(!w||!w->journal||!changes||count==0u||count>UINT32_MAX)return AV_JOURNAL_INVALID;
    for(i=0;i<count;i++){
        size_t oldn=changes[i].old_length,newn=changes[i].new_length;
        if((oldn&&!changes[i].old_value)||(newn&&!changes[i].new_value)||oldn>UINT32_MAX||newn>UINT32_MAX||
           oldn>SIZE_MAX-12u||newn>SIZE_MAX-12u-oldn||total>SIZE_MAX-12u-oldn-newn)return AV_JOURNAL_INVALID;
        total+=12u+oldn+newn;
    }
    if(total>w->work_size||total>UINT32_MAX)return AV_JOURNAL_BOUNDS;
    if(!w->recovered){rc=av_wal_recover(w);if(rc!=AV_JOURNAL_OK)return rc;}
    pos=0;w32(w->work,(uint32_t)count);pos=4u;
    for(i=0;i<count;i++){
        w32(w->work+pos,changes[i].key);w32(w->work+pos+4u,(uint32_t)changes[i].old_length);w32(w->work+pos+8u,(uint32_t)changes[i].new_length);pos+=12u;
        if(changes[i].old_length)memcpy(w->work+pos,changes[i].old_value,changes[i].old_length);
        pos+=changes[i].old_length;
        if(changes[i].new_length)memcpy(w->work+pos,changes[i].new_value,changes[i].new_length);
        pos+=changes[i].new_length;
    }
    rc=av_journal_append(w->journal,WAL_PREPARE_BATCH,w->work,total,&prepare_seq);
    if(rc!=AV_JOURNAL_OK){if(rc==AV_JOURNAL_IO||rc==AV_JOURNAL_TORN)w->recovered=0;return rc;}
    w->recovered=0;
    if(walk_changes(w,w->work,total,1,0)!=0){
        int rollback=walk_changes(w,w->work,total,0,0);
        if(rollback!=0)return AV_JOURNAL_IO;
        w64(finish,prepare_seq);finish[8]=0;
        rc=av_journal_append(w->journal,WAL_FINISH,finish,sizeof finish,NULL);
        if(rc==AV_JOURNAL_OK)w->recovered=1;
        return rc==AV_JOURNAL_OK?AV_JOURNAL_IO:rc;
    }
    w64(finish,prepare_seq);finish[8]=1;
    rc=av_journal_append(w->journal,WAL_FINISH,finish,sizeof finish,NULL);
    if(rc==AV_JOURNAL_OK)w->recovered=1;
    return rc;
}

av_journal_status av_wal_update(av_wal *w,uint32_t key,const void *oldv,size_t oldn,const void *newv,size_t newn)
{
    av_wal_change c;
    c.key=key;c.old_value=oldv;c.old_length=oldn;c.new_value=newv;c.new_length=newn;
    return av_wal_update_many(w,&c,1u);
}

typedef struct {av_wal *wal;uint64_t pending_seq;size_t pending_len;uint16_t pending_type;uint8_t pending;uint8_t invalid;uint8_t io_error;} recover_state;

static int recover_visit(void *ctx,const av_journal_record *r,const uint8_t *p)
{
    recover_state *s=(recover_state *)ctx;av_wal *w=s->wal;
    if(r->type==WAL_PREPARE||r->type==WAL_PREPARE_BATCH){
        int legacy=(r->type==WAL_PREPARE);
        if(s->pending||r->length<(legacy?12u:4u)||r->length>w->work_size){s->invalid=1;return 1;}
        memcpy(w->work,p,r->length);
        /* Validate the full list without applying any change. */
        {
            size_t pos=legacy?0u:4u,i;uint32_t count=legacy?1u:r32(w->work);
            if(count==0u){s->invalid=1;return 1;}
            for(i=0;i<(size_t)count;i++){
                uint32_t oldn,newn;
                if(pos>r->length||r->length-pos<12u){s->invalid=1;return 1;}
                oldn=r32(w->work+pos+4u);newn=r32(w->work+pos+8u);pos+=12u;
                if((size_t)oldn>r->length-pos||(size_t)newn>r->length-pos-(size_t)oldn){s->invalid=1;return 1;}
                pos+=(size_t)oldn+(size_t)newn;
            }
            if(pos!=r->length){s->invalid=1;return 1;}
        }
        s->pending_seq=r->sequence;s->pending_len=r->length;s->pending_type=r->type;s->pending=1;
    }else if(r->type==WAL_FINISH){
        if(r->length!=9u||!s->pending||r64(p)!=s->pending_seq||p[8]>1u){s->invalid=1;return 1;}
        if(p[8]&&walk_changes(w,w->work,s->pending_len,1,s->pending_type==WAL_PREPARE)!=0){s->io_error=1;return 1;}
        s->pending=0;
    }
    return 0;
}

av_journal_status av_wal_recover(av_wal *w)
{
    recover_state s;av_journal_status rc;
    if(!w||!w->journal)return AV_JOURNAL_INVALID;
    w->recovered=0;memset(&s,0,sizeof s);s.wal=w;
    rc=av_journal_scan(w->journal,recover_visit,&s);
    if(s.invalid)return AV_JOURNAL_CORRUPT;
    if(s.io_error)return AV_JOURNAL_IO;
    if(rc!=AV_JOURNAL_OK&&rc!=AV_JOURNAL_TORN)return rc;
    if(s.pending){
        uint8_t finish[9];
        if(walk_changes(w,w->work,s.pending_len,0,s.pending_type==WAL_PREPARE)!=0)return AV_JOURNAL_IO;
        if(rc==AV_JOURNAL_OK){
            w64(finish,s.pending_seq);finish[8]=0;
            rc=av_journal_append(w->journal,WAL_FINISH,finish,sizeof finish,NULL);
            if(rc!=AV_JOURNAL_OK)return rc;
        }
    }
    w->recovered=(uint8_t)(rc==AV_JOURNAL_OK);
    return rc;
}
