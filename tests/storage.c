#include "aervault.h"
#include "aervault_budget.h"
#include "aervault_diag.h"
#include "aervault_history.h"
#ifdef AV_TEST_NOR
#include "aervault_nor.h"
#endif
#include "aervault_queue.h"
#include "aervault_wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"storage line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef struct { uint8_t bytes[8192]; unsigned programs; unsigned fail_at; unsigned partial; unsigned syncs; unsigned fail_sync_at; } flash_mem;
static int f_read(void*c,uint32_t o,void*b,size_t n){flash_mem*m=(flash_mem*)c;if((uint64_t)o+n>sizeof m->bytes)return -1;memcpy(b,m->bytes+o,n);return 0;}
static int f_program(void*c,uint32_t o,const void*b,size_t n){flash_mem*m=(flash_mem*)c;const uint8_t*p=(const uint8_t*)b;size_t i,lim=n;if(o%4u||n%4u||(uint64_t)o+n>sizeof m->bytes)return -1;m->programs++;if(m->fail_at==m->programs&&m->partial)lim=(n/2u)&~(size_t)3u;for(i=0;i<lim;i++){if((m->bytes[o+i]&p[i])!=p[i])return -2;m->bytes[o+i]&=p[i];}return m->fail_at==m->programs?-31:0;}
static int f_erase(void*c,uint32_t o,size_t n){flash_mem*m=(flash_mem*)c;if(o%256u||n%256u||(uint64_t)o+n>sizeof m->bytes)return -1;memset(m->bytes+o,0xff,n);return 0;}
static int nor_drv_read(uint32_t o,void*b,uint32_t n,void*c){return f_read(c,o,b,n);}
static int nor_drv_write(uint32_t o,const void*b,uint32_t n,void*c){return f_program(c,o,b,n);}
static int nor_drv_erase(uint32_t o,uint32_t n,void*c){return f_erase(c,o,n);}
static int f_sync(void*c){(void)c;return 0;}
static int f_sync_fault(void*c){flash_mem*m=(flash_mem*)c;m->syncs++;return m->syncs==m->fail_sync_at?-41:0;}
#ifdef AV_TEST_NOR
static void nor_adapter(void){flash_mem media;mflash_t flash;mflash_driver_t driver;mflash_partition_t part[1];mflash_stats_t stats[1];av_nor adapter;av_journal_io io;av_journal_layout layout;av_journal j;uint8_t work[512],out[8];uint32_t programs,erases;memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;driver.read=nor_drv_read;driver.write=nor_drv_write;driver.erase=nor_drv_erase;driver.ctx=&media;CHECK(mflash_init(&flash,&driver,part,stats,1)==MFLASH_OK);CHECK(mflash_add_partition(&flash,"persist",0,8192,256,4,MFLASH_TYPE_NOR,0)==0);CHECK(av_nor_bind(&adapter,&flash,0,f_sync,NULL,&io,&layout)==0);CHECK(layout.bank_size==4096&&layout.erase_size==256&&layout.program_size==4);CHECK(mflash_write(&flash,0,1,"1234",4)==MFLASH_ERR_ALIGN);CHECK(mflash_read(&flash,0,8190,out,8)==MFLASH_ERR_BOUNDS);CHECK(av_journal_format(&j,&io,&layout,work,sizeof work)==AV_JOURNAL_OK);CHECK(av_journal_append(&j,0x44,"nor",3,NULL)==AV_JOURNAL_OK);CHECK(av_nor_write_counts(&adapter,&programs,&erases)==0&&programs>=2&&erases==2);}
#endif
static int keep_all(void*c,const av_journal_record*r,const uint8_t*p){(void)c;(void)r;(void)p;return 1;}
typedef struct { uint8_t data[2][16];size_t len[2];uint32_t fail_key;unsigned calls; } value_store;
static int apply_value(void*c,uint32_t key,const void*p,size_t n){value_store*s=(value_store*)c;unsigned i;if(key<7||key>8||n>sizeof s->data[0])return -1;i=key-7u;s->calls++;if(s->fail_key==key)return -1;memset(s->data[i],0,sizeof s->data[i]);if(n)memcpy(s->data[i],p,n);s->len[i]=n;return 0;}
static int read_value(void*c,uint32_t key,void*p,size_t cap,size_t*n){value_store*s=(value_store*)c;unsigned i;if(key<7||key>8)return -1;i=key-7u;if(s->len[i]>cap)return -1;memcpy(p,s->data[i],s->len[i]);*n=s->len[i];return 0;}
typedef struct { uint8_t data[2][128];size_t len[2]; } object_store;
static int ob_read(void*c,uint32_t id,unsigned slot,uint8_t*out,size_t cap,size_t*n){object_store*m=(object_store*)c;if(id!=55||slot>1)return -1;if(!m->len[slot])return 1;if(m->len[slot]>cap)return -2;memcpy(out,m->data[slot],m->len[slot]);*n=m->len[slot];return 0;}
static int ob_replace(void*c,uint32_t id,unsigned slot,const uint8_t*p,size_t n){object_store*m=(object_store*)c;if(id!=55||slot>1||n>sizeof m->data[slot])return -1;memcpy(m->data[slot],p,n);m->len[slot]=n;return 0;}
static int ob_valid(void*c,uint32_t id){(void)c;return id==55;}
static int test_tag(void*c,const uint8_t*p,size_t n,uint8_t out[AV_TAG_BYTES]){uint32_t h=2166136261u;size_t i;(void)c;for(i=0;i<n;i++)h=(h^p[i])*16777619u;for(i=0;i<AV_TAG_BYTES;i++)out[i]=(uint8_t)(h>>(8u*(i%4u)));return 0;}
static void journal_and_recovery(void){
    flash_mem media;av_journal j,j2,j3;av_journal_io io;av_journal_layout layout;uint8_t journal_work[512],wal_work[256];uint64_t seq,id1,id2;av_journal_status s;av_wal wal;value_store val;av_queue q,q2;av_queue_item items[8],items2[8];char output[16];size_t out_len;
    memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;io.ctx=&media;io.read=f_read;io.program=f_program;io.erase=f_erase;io.sync=f_sync;layout.bank_offset[0]=0;layout.bank_offset[1]=4096;layout.bank_size=4096;layout.erase_size=256;layout.program_size=4;layout.erased_value=0xff;
    CHECK(av_journal_format(&j,&io,&layout,journal_work,sizeof journal_work)==AV_JOURNAL_OK);
    CHECK(av_journal_append(&j,0x77,"base",4,&seq)==AV_JOURNAL_OK&&seq==1);
    CHECK(av_queue_open(&q,&j,items,8)==AV_JOURNAL_OK);
    CHECK(av_queue_push(&q,"first",5,&id1)==AV_JOURNAL_OK);
    CHECK(av_queue_push(&q,"second",6,&id2)==AV_JOURNAL_OK&&id2>id1);
    CHECK(av_queue_ack(&q,id1)==AV_JOURNAL_OK);
    CHECK(av_queue_peek(&q,&seq,output,sizeof output,&out_len)==AV_JOURNAL_OK&&seq==id2&&out_len==6&&memcmp(output,"second",6)==0);
    memset(&val,0,sizeof val);memcpy(val.data[0],"old",3);val.len[0]=3;
    CHECK(av_wal_init(&wal,&j,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
    CHECK(av_wal_update(&wal,7,"old",3,"new",3)==AV_JOURNAL_OK&&memcmp(val.data[0],"new",3)==0);
    CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK&&memcmp(val.data[0],"new",3)==0);
    /* Fail after the new value reached the data callback, while persisting the WAL finish. */
    media.partial=1;media.fail_at=media.programs+4u;
    CHECK(av_wal_update(&wal,7,"new",3,"bad",3)==AV_JOURNAL_IO&&memcmp(val.data[0],"bad",3)==0);
    media.fail_at=0;media.partial=0;
    s=av_journal_mount(&j2,&io,&layout,journal_work,sizeof journal_work);CHECK(s==AV_JOURNAL_TORN);
    CHECK(av_wal_init(&wal,&j2,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
    CHECK(av_wal_recover(&wal)==AV_JOURNAL_TORN&&memcmp(val.data[0],"new",3)==0);
    CHECK(av_journal_compact(&j2,keep_all,NULL)==AV_JOURNAL_OK);
    CHECK(av_queue_open(&q2,&j2,items2,8)==AV_JOURNAL_OK&&q2.count==1&&items2[0].id==id2);
    CHECK(av_queue_peek(&q2,&seq,output,sizeof output,&out_len)==AV_JOURNAL_OK&&seq==id2&&out_len==6&&memcmp(output,"second",6)==0);
    CHECK(av_queue_compact(&q2)==AV_JOURNAL_OK);
    CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK&&memcmp(val.data[0],"new",3)==0);
    /* A write error after the commit marker reached media is uncertain to the caller,
       but recovery must discover the durable finish and retain the new value. */
    media.fail_at=media.programs+4u;
    CHECK(av_wal_update(&wal,7,"new",3,"done",4)==AV_JOURNAL_IO&&memcmp(val.data[0],"done",4)==0);
    media.fail_at=0;
    CHECK(av_journal_mount(&j,&io,&layout,journal_work,sizeof journal_work)==AV_JOURNAL_OK);
    CHECK(av_wal_init(&wal,&j,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
    CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK&&memcmp(val.data[0],"done",4)==0);
    /* A partially programmed prepare record is rejected as an incomplete tail. */
    media.partial=1;media.fail_at=media.programs+1u;
    CHECK(av_wal_update(&wal,7,"done",4,"lost",4)==AV_JOURNAL_IO&&memcmp(val.data[0],"done",4)==0);
    media.fail_at=0;media.partial=0;
    CHECK(av_journal_mount(&j2,&io,&layout,journal_work,sizeof journal_work)==AV_JOURNAL_TORN);
    CHECK(av_wal_init(&wal,&j2,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
    CHECK(av_wal_recover(&wal)==AV_JOURNAL_TORN&&memcmp(val.data[0],"done",4)==0);
    CHECK(av_journal_compact(&j2,keep_all,NULL)==AV_JOURNAL_OK);
    /* Multi-object transaction: any recovery callback failure keeps reads gated;
       retry from a clean journal/context converges to the committed whole state. */
    {
        av_wal_change changes[2];uint8_t out[16];size_t n=0;
        memset(&val,0,sizeof val);memcpy(val.data[0],"a0",2);val.len[0]=2;memcpy(val.data[1],"b0",2);val.len[1]=2;
        CHECK(av_wal_init(&wal,&j2,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
        CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK);
        changes[0].key=7;changes[0].old_value="a0";changes[0].old_length=2;changes[0].new_value="a1";changes[0].new_length=2;
        changes[1].key=8;changes[1].old_value="b0";changes[1].old_length=2;changes[1].new_value="b1";changes[1].new_length=2;
        CHECK(av_wal_update_many(&wal,changes,2)==AV_JOURNAL_OK);
        CHECK(av_wal_read(&wal,read_value,7,out,sizeof out,&n)==AV_JOURNAL_OK&&n==2&&memcmp(out,"a1",2)==0);
        CHECK(av_wal_read(&wal,read_value,8,out,sizeof out,&n)==AV_JOURNAL_OK&&n==2&&memcmp(out,"b1",2)==0);
        memcpy(val.data[0],"a0",2);memcpy(val.data[1],"b0",2);val.calls=0;val.fail_key=8;
        CHECK(av_journal_mount(&j3,&io,&layout,journal_work,sizeof journal_work)==AV_JOURNAL_OK);
        CHECK(av_wal_init(&wal,&j3,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
        s=av_wal_recover(&wal);CHECK(s==AV_JOURNAL_IO&&!av_wal_is_ready(&wal));
        CHECK(memcmp(val.data[0],"a1",2)==0&&memcmp(val.data[1],"b0",2)==0);
        CHECK(av_wal_read(&wal,read_value,7,out,sizeof out,&n)==AV_JOURNAL_NOT_READY);
        val.fail_key=0;val.calls=0;
        CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK&&av_wal_is_ready(&wal));
        CHECK(av_wal_read(&wal,read_value,7,out,sizeof out,&n)==AV_JOURNAL_OK&&n==2&&memcmp(out,"a1",2)==0);
        CHECK(av_wal_read(&wal,read_value,8,out,sizeof out,&n)==AV_JOURNAL_OK&&n==2&&memcmp(out,"b1",2)==0);
        {
            uint8_t legacy[16],finish[9];uint64_t prepare;unsigned k;
            legacy[0]=7;legacy[1]=legacy[2]=legacy[3]=0;legacy[4]=2;legacy[5]=legacy[6]=legacy[7]=0;legacy[8]=2;legacy[9]=legacy[10]=legacy[11]=0;memcpy(legacy+12,"c0c1",4);
            CHECK(av_journal_append(&j3,0x1001u,legacy,sizeof legacy,&prepare)==AV_JOURNAL_OK);
            for(k=0;k<8;k++){finish[k]=(uint8_t)(prepare>>(8u*k));}finish[8]=1;
            CHECK(av_journal_append(&j3,0x1002u,finish,sizeof finish,NULL)==AV_JOURNAL_OK);
            memset(&val,0,sizeof val);val.calls=0;
            CHECK(av_wal_init(&wal,&j3,wal_work,sizeof wal_work,apply_value,&val)==AV_JOURNAL_OK);
            CHECK(av_wal_recover(&wal)==AV_JOURNAL_OK&&memcmp(val.data[0],"c1",2)==0&&memcmp(val.data[1],"b1",2)==0);
        }
    }
}
static void journal_publication_and_corruption(void){flash_mem media;av_journal_io io;av_journal_layout layout;av_journal j,j2;uint8_t work[512];uint64_t seq;memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;media.syncs=0;media.fail_sync_at=0;io.ctx=&media;io.read=f_read;io.program=f_program;io.erase=f_erase;io.sync=f_sync_fault;layout.bank_offset[0]=0;layout.bank_offset[1]=4096;layout.bank_size=4096;layout.erase_size=256;layout.program_size=4;layout.erased_value=0xff;CHECK(av_journal_format(&j,&io,&layout,work,sizeof work)==AV_JOURNAL_OK);CHECK(av_journal_append(&j,0x77,"kept",4,&seq)==AV_JOURNAL_OK&&seq==1);media.fail_sync_at=media.syncs+5u;CHECK(av_journal_compact(&j,keep_all,NULL)==AV_JOURNAL_IO);CHECK(av_journal_append(&j,0x77,"blocked",7,NULL)==AV_JOURNAL_NOT_READY);media.fail_sync_at=0;CHECK(av_journal_mount(&j2,&io,&layout,work,sizeof work)==AV_JOURNAL_OK);CHECK(av_journal_next_sequence(&j2)==2);CHECK(av_journal_append(&j2,0x77,"next",4,&seq)==AV_JOURNAL_OK&&seq==2);
    memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;media.syncs=0;media.fail_sync_at=0;io.sync=f_sync;CHECK(av_journal_format(&j,&io,&layout,work,sizeof work)==AV_JOURNAL_OK);CHECK(av_journal_append(&j,0x77,"valid",5,NULL)==AV_JOURNAL_OK);media.bytes[256]^=0x02u;CHECK(av_journal_mount(&j2,&io,&layout,work,sizeof work)==AV_JOURNAL_CORRUPT);
    memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;media.syncs=0;media.fail_sync_at=0;CHECK(av_journal_format(&j,&io,&layout,work,sizeof work)==AV_JOURNAL_OK);CHECK(av_journal_append(&j,0x77,"valid",5,NULL)==AV_JOURNAL_OK);media.bytes[260]^=0x01u;CHECK(av_journal_compact(&j,keep_all,NULL)==AV_JOURNAL_CORRUPT);CHECK(av_journal_append(&j,0x77,"blocked",7,NULL)==AV_JOURNAL_NOT_READY);}
static void history_diag_budget(void){
    flash_mem media;av_journal j;av_journal_io io;av_journal_layout layout;uint8_t journal_work[512],hist_work[128],core_work[120];object_store os;av_backend backend;av_object_rule rule;av_config cfg;av_context context;av_history hist;av_diag diag;av_diag_record rec,got;uint64_t seq,rev;size_t n;av_budget budget;
    memset(&media,0xff,sizeof media);media.programs=0;media.fail_at=0;media.partial=0;io.ctx=&media;io.read=f_read;io.program=f_program;io.erase=f_erase;io.sync=f_sync;layout.bank_offset[0]=0;layout.bank_offset[1]=4096;layout.bank_size=4096;layout.erase_size=256;layout.program_size=4;layout.erased_value=0xff;CHECK(av_journal_format(&j,&io,&layout,journal_work,sizeof journal_work)==AV_JOURNAL_OK);
    memset(&os,0,sizeof os);backend.ctx=&os;backend.read=ob_read;backend.replace=ob_replace;backend.validate_id=ob_valid;memset(&rule,0,sizeof rule);rule.id=55;rule.identity[0]=3;rule.schema=1;rule.policy=AV_RECOVERABLE;rule.payload_limit=8;cfg.backend=&backend;cfg.objects=&rule;cfg.object_count=1;cfg.work=core_work;cfg.work_size=sizeof core_work;cfg.tag=test_tag;cfg.tag_ctx=NULL;CHECK(av_open(&context,&cfg)==AV_OK);CHECK(av_provision_id(&context,55,"v0",2,NULL)==AV_OK);CHECK(av_put(&context,55,"v1",2,NULL)==AV_OK);
    CHECK(av_history_init(&hist,&j,hist_work,sizeof hist_work,2)==AV_JOURNAL_OK);CHECK(av_history_checkpoint(&hist,&context,55,&seq)==AV_JOURNAL_OK);CHECK(av_get_revision(&context,55,&rev,NULL)==AV_OK&&rev==3);
    CHECK(av_put(&context,55,"v2",2,NULL)==AV_OK);CHECK(av_history_checkpoint(&hist,&context,55,NULL)==AV_JOURNAL_OK);CHECK(av_history_latest(&hist,55,&rev,hist_work,sizeof hist_work,&n)==AV_JOURNAL_OK&&rev==4&&n==2&&memcmp(hist_work,"v2",2)==0);CHECK(av_history_compact(&hist)==AV_JOURNAL_OK);
    CHECK(av_diag_init(&diag,&j,1)==AV_JOURNAL_OK);memset(&rec,0,sizeof rec);rec.timestamp=123;rec.event=9;rec.severity=2;rec.length=2;rec.payload[0]=0xaa;rec.payload[1]=0xbb;CHECK(av_diag_write(&diag,&rec,&seq)==AV_JOURNAL_OK);rec.event=10;CHECK(av_diag_write(&diag,&rec,&rev)==AV_JOURNAL_OK);CHECK(av_diag_compact(&diag)==AV_JOURNAL_OK);CHECK(av_diag_read(&diag,rev,&got)==AV_JOURNAL_OK&&got.event==10&&got.length==2&&got.payload[1]==0xbb);
    av_budget_init(&budget,10,4,0,0);CHECK(av_budget_check(&budget,3,1)==AV_BUDGET_ALLOW);CHECK(av_budget_charge(&budget,10,0)==AV_BUDGET_ALLOW);CHECK(av_budget_check(&budget,1,0)==AV_BUDGET_LIMIT_PROGRAM);CHECK(av_budget_check(&budget,0,5)==AV_BUDGET_LIMIT_ERASE);
}
int main(void){
#ifdef AV_TEST_NOR
nor_adapter();
#endif
journal_and_recovery();journal_publication_and_corruption();history_diag_budget();puts("Aervault storage mechanisms: PASS");return 0;}
