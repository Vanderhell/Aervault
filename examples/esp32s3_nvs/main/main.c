#include "aervault.h"
#include "aervault_journal.h"
#include "aervault_nvs.h"
#include "aervault_nor.h"
#include "aervault_seal.h"
#include "aervault_pair.h"
#include "aervault_wal.h"
#include "aervault_queue.h"
#include "aervault_history.h"
#include "aervault_diag.h"
#include "aervault_budget.h"
#include "mdh.h"
#include "mcrypt.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t counter_work[2 * (AV_OVERHEAD + 8)];
static uint8_t security_work[2 * (AV_OVERHEAD + 8)];
static av_nvs counter_store, security_store;
static av_object counter_object, security_object;
static char device_mac[18];
static esp_reset_reason_t startup_reset_reason;
static uint32_t stack_free_min = UINT32_MAX, stack_free_boot;

/* Separate NVS namespaces keep the nonce counter and sealed blob isolated. */
enum { SEAL_COUNTER_ID = 77 };
static uint8_t seal_work[2 * (AV_OVERHEAD + 8)];
static av_nvs seal_counter_store, sealed_blob_store;
static av_nvs_route seal_route;
static av_nvs_router seal_router;
static av_backend seal_backend;
static av_object_rule seal_rule;
static av_context seal_vault;
static av_seal seal_context;
static const uint8_t seal_key[AV_SEAL_KEY_BYTES] =
    {0xA7,0x11,0xB2,0x23,0xC3,0x34,0xD4,0x45,0xE5,0x56,0xF6,0x67,0x17,0x78,0x28,0x89};
static const uint8_t seal_prefix[4] = {0xA1,0xE1,0x01,0x01};

/* Test-only NOR driver uses only the isolated avraw partition. */
static const esp_partition_t *raw_partition;
static mflash_t mflash;
static mflash_partition_t mflash_parts[1];
static mflash_stats_t mflash_stats[1];
static av_nor nor_adapter;
static av_journal_io nor_io;
static av_journal_layout nor_layout;
static av_journal nor_log;
static uint8_t nor_work[1024];
static uint64_t nor_keep_sequence;
static unsigned nor_seen;
static uint32_t nor_last_value;

static uint8_t pair_private_a[32] = {1};
static uint8_t pair_private_b[32] = {2};
static uint8_t pair_public_a[32], pair_public_b[32];
static uint8_t pair_id_a[16], pair_id_b[16];
static int pair_is_a;
static av_pair_keys pair_keys;
static const uint8_t pair_psk[32] = {
    0x31,0x42,0x53,0x64,0x75,0x86,0x97,0xA8,0xB9,0xCA,0xDB,0xEC,0xFD,0x0E,0x1F,0x20,
    0x41,0x52,0x63,0x74,0x85,0x96,0xA7,0xB8,0xC9,0xDA,0xEB,0xFC,0x0D,0x1E,0x2F,0x30};
static const uint8_t pair_salt[16] = {0x50,0x41,0x49,0x52,0x2D,0x54,0x45,0x53,0x54,1,2,3,4,5,6,7};

enum { HISTORY_ID_A = 88, HISTORY_ID_B = 89 };
static av_nvs history_store_a, history_store_b;
static av_nvs_route history_routes[2];
static av_nvs_router history_router;
static av_backend history_backend;
static av_object_rule history_rules[2];
static av_context history_vault;
static uint8_t history_context_work[2 * (AV_OVERHEAD + 16)];
static uint8_t history_module_work[1024];
static av_history history_log;
static av_wal wal_log;
static av_queue queue_log;
static av_queue_item queue_items[16];
static av_diag diag_log;
static uint64_t diag_latest_sequence;
static uint32_t recovered_diag_event;
static size_t recovered_diag_count;
typedef struct { int32_t value[2]; } wal_model_t;
static wal_model_t wal_model;

static int firmware_test_tag(void *ctx, const uint8_t *data, size_t size,
                             uint8_t out[AV_TAG_BYTES])
{
    uint32_t h = UINT32_C(2166136261); size_t i;
    (void)ctx;
    while (size--) h = (h ^ *data++) * UINT32_C(16777619);
    for (i = 0; i < AV_TAG_BYTES; ++i) out[i] = (uint8_t)(h >> (8 * (i & 3)));
    return 0;
}

static void report(const char *label, av_status status, av_nvs *store, av_error *e)
{
    printf("%s,status=%d,stage=%d,slot=%u,esp_err=0x%x,operation=%s\n",
           label, (int)status, e ? (int)e->stage : 0, e ? e->slot : 0,
           (unsigned)store->last_error,
           store->last_operation ? store->last_operation : "none");
}

static void sample_stack(void)
{
    uint32_t free_now = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (free_now < stack_free_min) stack_free_min = free_now;
}

static int raw_read(uint32_t addr, void *buf, uint32_t len, void *ctx)
{ (void)ctx; return raw_partition ? (int)esp_partition_read(raw_partition, addr, buf, len) : -1; }
static int raw_write(uint32_t addr, const void *buf, uint32_t len, void *ctx)
{ (void)ctx; return raw_partition ? (int)esp_partition_write(raw_partition, addr, buf, len) : -1; }
static int raw_erase(uint32_t addr, uint32_t len, void *ctx)
{ (void)ctx; return raw_partition ? (int)esp_partition_erase_range(raw_partition, addr, len) : -1; }
static int raw_sync(void *ctx) { (void)ctx; return 0; }

static int nor_setup(void)
{
    mflash_driver_t driver = {raw_read, raw_write, raw_erase, NULL};
    mflash_err_t st;
    raw_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                              (esp_partition_subtype_t)0x40, "avraw");
    if (!raw_partition || raw_partition->size < 4u * 4096u) return -1;
    st = mflash_init(&mflash, &driver, mflash_parts, mflash_stats, 1);
    if (st != MFLASH_OK) return -1;
    if (mflash_add_partition(&mflash, "avraw", 0, raw_partition->size,
                             4096, 4, MFLASH_TYPE_NOR, false) < 0) return -1;
    return av_nor_bind(&nor_adapter, &mflash, 0, raw_sync, NULL,
                       &nor_io, &nor_layout);
}

static int nor_keep_latest(void *ctx, const av_journal_record *r, const uint8_t *payload)
{ (void)ctx; (void)payload; return r->sequence == nor_keep_sequence; }

static int nor_visit_latest(void *ctx, const av_journal_record *r, const uint8_t *payload)
{
    (void)ctx;
    if (r->length < sizeof nor_last_value) return -1;
    memcpy(&nor_last_value, payload, sizeof nor_last_value);
    ++nor_seen;
    return 0;
}

static int nor_test(void)
{
    uint8_t payload[64]; uint32_t i; uint64_t sequence = 0;
    av_journal_status st;
    memset(&nor_log, 0, sizeof nor_log);
    st = av_journal_format(&nor_log, &nor_io, &nor_layout, nor_work, sizeof nor_work);
    if (st != AV_JOURNAL_OK) { printf("NOR_FORMAT_FAIL,status=%d\n", (int)st); return -1; }
    for (i = 0; i < 40; ++i) {
        memset(payload, (int)i, sizeof payload);
        memcpy(payload, &i, sizeof i);
        st = av_journal_append(&nor_log, 1, payload, sizeof payload, &sequence);
        if (st != AV_JOURNAL_OK) { printf("NOR_APPEND_FAIL,index=%u,status=%d\n", (unsigned)i, (int)st); return -1; }
    }
    nor_keep_sequence = sequence;
    st = av_journal_compact(&nor_log, nor_keep_latest, NULL);
    if (st != AV_JOURNAL_OK || nor_log.active_bank != 1u) {
        printf("NOR_COMPACT_FAIL,status=%d,bank=%u\n", (int)st, (unsigned)nor_log.active_bank); return -1;
    }
    return 0;
}

static int nor_mount_check(void)
{
    av_journal_status st;
    memset(&nor_log, 0, sizeof nor_log);
    st = av_journal_mount(&nor_log, &nor_io, &nor_layout, nor_work, sizeof nor_work);
    if (st != AV_JOURNAL_OK && st != AV_JOURNAL_TORN) {
        printf("NOR_MOUNT_FAIL,status=%d\n", (int)st); return -1;
    }
    nor_seen = 0; nor_last_value = UINT32_MAX;
    st = av_journal_scan(&nor_log, nor_visit_latest, NULL);
    if (st != AV_JOURNAL_OK || nor_seen != 1 || nor_last_value != 39) {
        printf("NOR_RECOVERY_FAIL,status=%d,count=%u,value=%u\n", (int)st, nor_seen, (unsigned)nor_last_value); return -1;
    }
    printf("NOR_RECOVERY_PASS,bank=%u,generation=%u,records=%u,last=%u\n",
           (unsigned)nor_log.active_bank, (unsigned)nor_log.generation, nor_seen, (unsigned)nor_last_value);
    return 0;
}

static int seal_setup(void)
{
    av_config cfg;
    if (av_nvs_open(&seal_counter_store, "avtest", "avnonce", NVS_READWRITE) != ESP_OK ||
        av_nvs_open(&sealed_blob_store, "avtest", "avcipher", NVS_READWRITE) != ESP_OK) return -1;
    seal_route.object_id = SEAL_COUNTER_ID; seal_route.store = &seal_counter_store;
    seal_router.routes = &seal_route; seal_router.count = 1;
    if (av_nvs_backend_bind(&seal_router, &seal_backend) != ESP_OK) return -1;
    memset(&seal_rule, 0, sizeof seal_rule);
    seal_rule.id = SEAL_COUNTER_ID; memset(seal_rule.identity, 0x77, sizeof seal_rule.identity);
    seal_rule.schema = 1; seal_rule.policy = AV_COUNTER; seal_rule.payload_limit = 8;
    memset(&cfg, 0, sizeof cfg); cfg.backend = &seal_backend; cfg.objects = &seal_rule;
    cfg.object_count = 1; cfg.work = seal_work; cfg.work_size = sizeof seal_work;
    cfg.tag = firmware_test_tag;
    if (av_open(&seal_vault, &cfg) != AV_OK) return -1;
    return (int)av_seal_init(&seal_context, &seal_vault, SEAL_COUNTER_ID, seal_key, seal_prefix);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *src, uint8_t *dst, size_t bytes)
{
    size_t i;
    if (strlen(src) != bytes * 2u) return -1;
    for (i = 0; i < bytes; ++i) {
        int h = hex_digit(src[i * 2]), l = hex_digit(src[i * 2 + 1]);
        if (h < 0 || l < 0) return -1;
        dst[i] = (uint8_t)((h << 4) | l);
    }
    return 0;
}

static void hex_print(const uint8_t *p, size_t n)
{ size_t i; for (i = 0; i < n; ++i) printf("%02x", p[i]); }

typedef struct { const uint8_t *identity; const uint8_t *public_key; } pair_pin;
static int verify_pair_peer(void *ctx, const uint8_t id[16], const uint8_t pub[32])
{
    const pair_pin *pin = (const pair_pin *)ctx;
    return memcmp(id, pin->identity, 16) == 0 && memcmp(pub, pin->public_key, 32) == 0 ? 0 : -1;
}

static int pair_setup(void)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return -1;
    pair_is_a = mac[5] == 0x44;
    memset(pair_id_a, 0, sizeof pair_id_a); memset(pair_id_b, 0, sizeof pair_id_b);
    memcpy(pair_id_a, (const uint8_t[]){0x3c,0x0f,0x02,0xde,0x77,0x44}, 6);
    memcpy(pair_id_b, (const uint8_t[]){0x3c,0x0f,0x02,0xde,0x77,0xfc}, 6);
    if (mdh_public_key(pair_public_a, pair_private_a) != MDH_OK ||
        mdh_public_key(pair_public_b, pair_private_b) != MDH_OK) return -1;
    return 0;
}

static int derive_pair(int use_x25519, av_pair_keys *keys)
{
    const uint8_t *local = pair_is_a ? pair_id_a : pair_id_b;
    const uint8_t *peer = pair_is_a ? pair_id_b : pair_id_a;
    const uint8_t *peer_pub = pair_is_a ? pair_public_b : pair_public_a;
    const uint8_t *private_key = pair_is_a ? pair_private_a : pair_private_b;
    pair_pin pin = {peer, peer_pub};
    if (use_x25519)
        return av_pair_x25519(private_key, local, peer, peer_pub, verify_pair_peer,
                              &pin, pair_salt, sizeof pair_salt, pair_is_a, keys);
    return av_pair_derive(pair_psk, sizeof pair_psk, pair_salt, sizeof pair_salt,
                          local, peer, peer_pub, verify_pair_peer, &pin, pair_is_a, keys);
}

static int history_context_setup(void)
{
    av_config cfg;
    if (av_nvs_open(&history_store_a, "avtest", "avhist1", NVS_READWRITE) != ESP_OK ||
        av_nvs_open(&history_store_b, "avtest", "avhist2", NVS_READWRITE) != ESP_OK) return -1;
    history_routes[0].object_id = HISTORY_ID_A; history_routes[0].store = &history_store_a;
    history_routes[1].object_id = HISTORY_ID_B; history_routes[1].store = &history_store_b;
    history_router.routes = history_routes; history_router.count = 2;
    if (av_nvs_backend_bind(&history_router, &history_backend) != ESP_OK) return -1;
    memset(history_rules, 0, sizeof history_rules);
    history_rules[0].id = HISTORY_ID_A; memset(history_rules[0].identity, 0x88, 16);
    history_rules[1].id = HISTORY_ID_B; memset(history_rules[1].identity, 0x89, 16);
    history_rules[0].schema = history_rules[1].schema = 1;
    history_rules[0].policy = history_rules[1].policy = AV_STRICT;
    history_rules[0].payload_limit = history_rules[1].payload_limit = 16;
    memset(&cfg, 0, sizeof cfg); cfg.backend = &history_backend; cfg.objects = history_rules;
    cfg.object_count = 2; cfg.work = history_context_work; cfg.work_size = sizeof history_context_work;
    cfg.tag = firmware_test_tag;
    return (int)av_open(&history_vault, &cfg);
}

static int wal_apply(void *ctx, uint32_t key, const void *value, size_t length)
{
    int32_t v;
    if (!ctx || key < 1u || key > 2u || length != sizeof v || !value) return -1;
    memcpy(&v, value, sizeof v); ((wal_model_t *)ctx)->value[key - 1u] = v;
    return 0;
}

static int wal_read_model(void *ctx, uint32_t key, void *out, size_t cap, size_t *length)
{
    if (!ctx || key < 1u || key > 2u || !out || cap < sizeof(int32_t) || !length) return -1;
    memcpy(out, &((wal_model_t *)ctx)->value[key - 1u], sizeof(int32_t));
    *length = sizeof(int32_t); return 0;
}

static int module_history_prepare(void)
{
    static const uint8_t a1[] = {'A','1'}, a2[] = {'A','2'}, a3[] = {'A','3'};
    static const uint8_t b0[] = {'B','0'}, b1[] = {'B','1'};
    av_error e; av_status cs; av_journal_status js; uint64_t seq, rev;
    cs = av_provision_id(&history_vault, HISTORY_ID_A, "A0", 2, &e);
    if (cs != AV_OK && cs != AV_EXISTS) return -1;
    cs = av_provision_id(&history_vault, HISTORY_ID_B, b0, sizeof b0, &e);
    if (cs != AV_OK && cs != AV_EXISTS) return -1;
    js = av_history_init(&history_log, &nor_log, history_module_work,
                         sizeof history_module_work, 3);
    if (js != AV_JOURNAL_OK) return -1;
#define HIST_PUT_CHECKPOINT(id_, data_, len_) do { \
    cs = av_put(&history_vault, (id_), (data_), (len_), &e); \
    if (cs != AV_OK) return -1; \
    js = av_history_checkpoint(&history_log, &history_vault, (id_), &seq); \
    if (js != AV_JOURNAL_OK) return -1; \
} while (0)
    HIST_PUT_CHECKPOINT(HISTORY_ID_A, a1, sizeof a1);
    HIST_PUT_CHECKPOINT(HISTORY_ID_A, a2, sizeof a2);
    HIST_PUT_CHECKPOINT(HISTORY_ID_B, b1, sizeof b1);
    HIST_PUT_CHECKPOINT(HISTORY_ID_A, a3, sizeof a3);
#undef HIST_PUT_CHECKPOINT
    js = av_history_compact(&history_log);
    if (js != AV_JOURNAL_OK) return -1;
    {
        uint8_t out[8]; size_t n = 0;
        if (av_history_latest(&history_log, HISTORY_ID_A, &rev, out, sizeof out, &n) != AV_JOURNAL_OK ||
            n != sizeof a3 || memcmp(out, a3, n)) return -1;
        if (av_history_latest(&history_log, HISTORY_ID_B, &rev, out, sizeof out, &n) != AV_JOURNAL_OK ||
            n != sizeof b1 || memcmp(out, b1, n)) return -1;
    }
    return 0;
}

static int diag_find_latest(void *ctx, const av_journal_record *r, const uint8_t *p)
{
    (void)ctx;
    if (r->type == 0x6001u && r->length == 46u) {
        ++recovered_diag_count;
        if (p[8] == 3u && p[9] == 0u && p[10] == 0u && p[11] == 0u) {
            diag_latest_sequence = r->sequence;
            recovered_diag_event = p[8];
        }
    }
    return 0;
}

static int modules_test(void)
{
    av_journal_status js; av_wal_change changes[2]; av_budget budget;
    av_diag_record dr; av_queue_item qitems[16]; uint64_t id_acked, id_pending, id;
    uint8_t payload[64], queue_out[32]; size_t n; uint32_t p0,e0,p1,e1;
    int32_t old_a=10, old_b=20, new_a=11, new_b=21, read_value;
    uint64_t before_tail;
    memset(&nor_log, 0, sizeof nor_log);
    js = av_journal_format(&nor_log, &nor_io, &nor_layout, nor_work, sizeof nor_work);
    if (js != AV_JOURNAL_OK) { printf("MODULES_FAIL,format=%d\n", (int)js); return -1; }
    wal_model.value[0] = old_a; wal_model.value[1] = old_b;
    if (av_wal_init(&wal_log, &nor_log, history_module_work,
                    sizeof history_module_work, wal_apply, &wal_model) != AV_JOURNAL_OK) return -1;
    changes[0] = (av_wal_change){1, &old_a, sizeof old_a, &new_a, sizeof new_a};
    changes[1] = (av_wal_change){2, &old_b, sizeof old_b, &new_b, sizeof new_b};
    js = av_wal_update_many(&wal_log, changes, 2);
    if (js != AV_JOURNAL_OK || wal_model.value[0] != new_a || wal_model.value[1] != new_b) {
        printf("MODULES_FAIL,wal_update=%d\n", (int)js); return -1;
    }
    if (av_wal_read(&wal_log, wal_read_model, 1, &read_value, sizeof read_value, &n) != AV_JOURNAL_OK ||
        n != sizeof read_value || read_value != new_a ||
        av_wal_read(&wal_log, wal_read_model, 2, &read_value, sizeof read_value, &n) != AV_JOURNAL_OK ||
        read_value != new_b) { puts("MODULES_FAIL,wal_read"); return -1; }

    if (av_queue_open(&queue_log, &nor_log, qitems, 16) != AV_JOURNAL_OK) return -1;
    if (av_queue_push(&queue_log, "acked", 5, &id_acked) != AV_JOURNAL_OK ||
        av_queue_push(&queue_log, "pending", 7, &id_pending) != AV_JOURNAL_OK ||
        av_queue_ack(&queue_log, id_acked) != AV_JOURNAL_OK ||
        av_queue_compact(&queue_log) != AV_JOURNAL_OK) { puts("MODULES_FAIL,queue_write"); return -1; }
    if (av_queue_peek(&queue_log, &id, queue_out, sizeof queue_out, &n) != AV_JOURNAL_OK ||
        id != id_pending || n != 7 || memcmp(queue_out, "pending", 7)) { puts("MODULES_FAIL,queue_peek"); return -1; }

    if (module_history_prepare() != 0) { puts("MODULES_FAIL,history"); return -1; }
    if (av_diag_init(&diag_log, &nor_log, 2) != AV_JOURNAL_OK) return -1;
    memset(&dr, 0, sizeof dr);
    for (id = 1; id <= 3; ++id) {
        dr.timestamp = 100u + id; dr.event = (uint32_t)id; dr.severity = 2;
        dr.length = 1; dr.payload[0] = (uint8_t)('0' + id);
        if (av_diag_write(&diag_log, &dr, &diag_latest_sequence) != AV_JOURNAL_OK) {
            puts("MODULES_FAIL,diag_write"); return -1;
        }
    }
    if (av_diag_compact(&diag_log) != AV_JOURNAL_OK ||
        av_diag_read(&diag_log, diag_latest_sequence, &dr) != AV_JOURNAL_OK || dr.event != 3) {
        puts("MODULES_FAIL,diag_retention"); return -1;
    }

    if (av_nor_write_counts(&nor_adapter, &p0, &e0) != 0) return -1;
    av_budget_init(&budget, p0, e0, p0, e0);
    before_tail = av_journal_free_bytes(&nor_log);
    if (av_budget_check(&budget, 1, 0) != AV_BUDGET_LIMIT_PROGRAM ||
        av_budget_charge(&budget, 1, 0) != AV_BUDGET_LIMIT_PROGRAM ||
        av_journal_free_bytes(&nor_log) != before_tail ||
        av_nor_write_counts(&nor_adapter, &p1, &e1) != 0 || p0 != p1 || e0 != e1) {
        puts("MODULES_FAIL,budget_refusal"); return -1;
    }
    printf("MODULES_TEST,PASS,wal=2_objects,queue=ack+pending,history=2_objects,diag=retained2,budget=program_limit\n");
    return 0;
}

static int modules_recover(void)
{
    av_journal_status js; av_error e; av_status cs; size_t n; uint8_t out[32];
    uint64_t rev, qid; int32_t expected_a=11, expected_b=21, read_value;
    js = av_journal_mount(&nor_log, &nor_io, &nor_layout, nor_work, sizeof nor_work);
    if (js != AV_JOURNAL_OK && js != AV_JOURNAL_TORN) { printf("MODULES_RECOVER_FAIL,mount=%d\n", (int)js); return -1; }
    wal_model.value[0] = 10; wal_model.value[1] = 20;
    if (av_wal_init(&wal_log, &nor_log, history_module_work,
                    sizeof history_module_work, wal_apply, &wal_model) != AV_JOURNAL_OK ||
        av_wal_recover(&wal_log) != AV_JOURNAL_OK ||
        wal_model.value[0] != expected_a || wal_model.value[1] != expected_b) {
        puts("MODULES_RECOVER_FAIL,wal"); return -1;
    }
    if (av_queue_open(&queue_log, &nor_log, queue_items, 16) != AV_JOURNAL_OK ||
        queue_log.count != 1 || av_queue_peek(&queue_log, &qid, out, sizeof out, &n) != AV_JOURNAL_OK ||
        n != 7 || memcmp(out, "pending", 7)) { puts("MODULES_RECOVER_FAIL,queue"); return -1; }
    if (av_history_init(&history_log, &nor_log, history_module_work,
                        sizeof history_module_work, 3) != AV_JOURNAL_OK ||
        av_history_latest(&history_log, HISTORY_ID_A, &rev, out, sizeof out, &n) != AV_JOURNAL_OK ||
        n != 2 || memcmp(out, "A3", 2) ||
        av_history_latest(&history_log, HISTORY_ID_B, &rev, out, sizeof out, &n) != AV_JOURNAL_OK ||
        n != 2 || memcmp(out, "B1", 2)) { puts("MODULES_RECOVER_FAIL,history"); return -1; }
    recovered_diag_count = 0; diag_latest_sequence = 0;
    js = av_journal_scan(&nor_log, diag_find_latest, NULL);
    if (js != AV_JOURNAL_OK || recovered_diag_count != 2 || !diag_latest_sequence ||
        av_diag_init(&diag_log, &nor_log, 2) != AV_JOURNAL_OK) {
        puts("MODULES_RECOVER_FAIL,diag_scan"); return -1;
    }
    {
        av_diag_record dr;
        if (av_diag_read(&diag_log, diag_latest_sequence, &dr) != AV_JOURNAL_OK || dr.event != 3) {
            puts("MODULES_RECOVER_FAIL,diag_read"); return -1;
        }
    }
    cs = av_get(&history_vault, HISTORY_ID_A, out, sizeof out, &n, &e);
    if (cs != AV_OK || n != 2 || memcmp(out, "A3", 2)) { puts("MODULES_RECOVER_FAIL,active_state"); return -1; }
    printf("MODULES_RECOVER,PASS,wal=%d/%d,queue_count=%u,history_objects=2,diag_latest=%u\n",
           (int)wal_model.value[0], (int)wal_model.value[1], (unsigned)queue_log.count,
           (unsigned)recovered_diag_event);
    return 0;
}

static int process_extra(char *command, char *arg)
{
    av_error e; av_status st;
    if (!strcmp(command, "METRICS")) {
        sample_stack();
        printf("METRICS,stack_high_water_bytes=%u,stack_free_at_boot_bytes=%u,stack_config_bytes=3584\n",
               (unsigned)stack_free_min, (unsigned)stack_free_boot);
    } else if (!strcmp(command, "NOR_TEST")) {
        int rc = nor_test(); sample_stack();
        printf("NOR_TEST,%s\n", rc == 0 ? "PASS" : "FAIL");
    } else if (!strcmp(command, "NOR_MOUNT")) {
        int rc = nor_mount_check(); sample_stack();
        printf("NOR_MOUNT,%s\n", rc == 0 ? "PASS" : "FAIL");
    } else if (!strcmp(command, "MODULES_TEST")) {
        int rc = modules_test(); sample_stack();
        printf("MODULES_TEST,%s\n", rc == 0 ? "PASS" : "FAIL");
    } else if (!strcmp(command, "MODULES_RECOVER")) {
        int rc = modules_recover(); sample_stack();
        printf("MODULES_RECOVER,%s\n", rc == 0 ? "PASS" : "FAIL");
    } else if (!strcmp(command, "SEAL_PROVISION")) {
        st = av_provision_id(&seal_vault, SEAL_COUNTER_ID, NULL, 0, &e);
        printf("SEAL_PROVISION,status=%d,native=%d\n", (int)st, e.native_code);
    } else if (!strcmp(command, "SEAL_WRITE")) {
        static const uint8_t plain[] = "AERVAULT-SEALED-RESTART";
        uint8_t nonce[AV_SEAL_NONCE_BYTES], tag[AV_SEAL_TAG_BYTES], cipher[sizeof plain - 1];
        uint8_t blob[1 + AV_SEAL_NONCE_BYTES + AV_SEAL_TAG_BYTES + sizeof cipher];
        esp_err_t ne;
        st = av_seal_encrypt(&seal_context, NULL, 0, plain, sizeof plain - 1,
                             cipher, nonce, tag, &e);
        if (st != AV_OK) { printf("SEAL_WRITE,status=%d,native=%d\n", (int)st, e.native_code); return 1; }
        blob[0] = (uint8_t)sizeof cipher; memcpy(blob + 1, nonce, sizeof nonce);
        memcpy(blob + 1 + sizeof nonce, tag, sizeof tag);
        memcpy(blob + 1 + sizeof nonce + sizeof tag, cipher, sizeof cipher);
        ne = nvs_set_blob(sealed_blob_store.handle, "obj", blob, sizeof blob);
        if (ne == ESP_OK) ne = nvs_commit(sealed_blob_store.handle);
        printf("SEAL_WRITE,status=%d,nonce=", (int)ne); hex_print(nonce, sizeof nonce); puts("");
    } else if (!strcmp(command, "SEAL_READ") || !strcmp(command, "SEAL_TAMPER")) {
        uint8_t blob[96], plain[64]; size_t n = sizeof blob; esp_err_t ne;
        static const uint8_t expected[] = "AERVAULT-SEALED-RESTART";
        ne = nvs_get_blob(sealed_blob_store.handle, "obj", blob, &n);
        if (ne != ESP_OK || n < 1 + AV_SEAL_NONCE_BYTES + AV_SEAL_TAG_BYTES ||
            blob[0] != n - 1 - AV_SEAL_NONCE_BYTES - AV_SEAL_TAG_BYTES) {
            printf("%s,status=FAIL,nvs=0x%x\n", command, (unsigned)ne); return 1;
        }
        memset(plain, 0xA5, sizeof plain);
        if (!strcmp(command, "SEAL_TAMPER")) blob[1 + AV_SEAL_NONCE_BYTES + AV_SEAL_TAG_BYTES] ^= 1;
        st = av_seal_decrypt(&seal_context, NULL, 0,
             blob + 1 + AV_SEAL_NONCE_BYTES + AV_SEAL_TAG_BYTES, blob[0],
             blob + 1, blob + 1 + AV_SEAL_NONCE_BYTES, plain);
        if (!strcmp(command, "SEAL_TAMPER")) {
            size_t i; int zero = 1; for (i = 0; i < blob[0]; ++i) if (plain[i]) zero = 0;
            printf("SEAL_TAMPER,%s\n", st == AV_AUTH_ERROR && zero ? "PASS_NO_PLAINTEXT" : "FAIL");
        } else {
            printf("SEAL_READ,%s,nonce=", st == AV_OK && blob[0] == sizeof expected - 1 &&
                   memcmp(plain, expected, sizeof expected - 1) == 0 ? "PASS" : "FAIL");
            hex_print(blob + 1, AV_SEAL_NONCE_BYTES); puts("");
        }
    } else if (!strcmp(command, "PAIR_KEYS") && arg) {
        int use_x = !strcmp(arg, "X25519");
        int rc = (!use_x && strcmp(arg, "PSK")) ? -1 : derive_pair(use_x, &pair_keys);
        if (rc) { printf("PAIR_KEYS,mode=%s,status=FAIL\n", arg); return 1; }
        printf("PAIR_KEYS,mode=%s,tx=", use_x ? "X25519" : "PSK");
        hex_print(pair_keys.tx_key, 32); printf(",rx="); hex_print(pair_keys.rx_key, 32); puts("");
    } else if (!strcmp(command, "PAIR_BAD") && arg) {
        int use_x = !strcmp(arg, "X25519"), rc; av_pair_keys bad;
        const uint8_t *peer = pair_is_a ? pair_id_b : pair_id_a;
        const uint8_t *peer_pub = pair_is_a ? pair_public_b : pair_public_a;
        const uint8_t *local = pair_is_a ? pair_id_a : pair_id_b;
        uint8_t bad_peer[16]; pair_pin pin = {peer, peer_pub};
        memcpy(bad_peer, peer, 16); bad_peer[0] ^= 0x80;
        if (use_x) rc = av_pair_x25519(pair_is_a ? pair_private_a : pair_private_b,
                      local, bad_peer, peer_pub, verify_pair_peer, &pin, pair_salt,
                      sizeof pair_salt, pair_is_a, &bad);
        else rc = av_pair_derive(pair_psk, sizeof pair_psk, pair_salt, sizeof pair_salt,
                      local, bad_peer, peer_pub, verify_pair_peer, &pin, pair_is_a, &bad);
        printf("PAIR_BAD,mode=%s,%s\n", use_x ? "X25519" : arg,
               rc != 0 ? "PASS_REJECTED" : "FAIL_ACCEPTED");
    } else if (!strcmp(command, "PAIR_ENC")) {
        static const uint8_t msg[] = "AERVAULT-MCU-PAIR";
        const uint8_t nonce[12] = {0x50,0x41,0x49,0x52,0x2d,0x4d,0x53,0x47,0,0,0,1};
        uint8_t cipher[sizeof msg - 1], tag[16]; mcrypt_aes128_t aes;
        if (derive_pair(0, &pair_keys) || mcrypt_aes128_init(&aes, pair_keys.tx_key) != MCRYPT_OK ||
            mcrypt_aes128_gcm_encrypt(&aes, nonce, NULL, 0, msg, sizeof msg - 1, cipher, tag) != MCRYPT_OK) {
            puts("PAIR_ENC,FAIL"); return 1;
        }
        printf("PAIR_PACKET,nonce="); hex_print(nonce, sizeof nonce); printf(",cipher=");
        hex_print(cipher, sizeof cipher); printf(",tag="); hex_print(tag, sizeof tag); puts("");
    } else if (!strcmp(command, "PAIR_DEC") && arg) {
        char nh[25], ch[35], th[33]; uint8_t nonce[12], cipher[17], tag[16], plain[32];
        static const uint8_t expected[] = "AERVAULT-MCU-PAIR"; mcrypt_aes128_t aes;
        int count = sscanf(arg, "%24s %34s %32s", nh, ch, th); mcrypt_status_t ms = MCRYPT_ERR_INVALID_ARGUMENT;
        if (count == 3 && !hex_decode(nh, nonce, sizeof nonce) &&
            !hex_decode(ch, cipher, sizeof cipher) && !hex_decode(th, tag, sizeof tag) &&
            !derive_pair(0, &pair_keys) && mcrypt_aes128_init(&aes, pair_keys.rx_key) == MCRYPT_OK) {
            memset(plain, 0xA5, sizeof plain);
            ms = mcrypt_aes128_gcm_decrypt(&aes, nonce, NULL, 0, cipher, sizeof cipher, tag, plain);
        }
        if (ms == MCRYPT_OK && !memcmp(plain, expected, sizeof expected - 1)) puts("PAIR_MESSAGE,PASS");
        else { memset(plain, 0, sizeof plain); puts("PAIR_MESSAGE,TAMPER_REJECTED_NO_PLAINTEXT"); }
    } else return 0;
    sample_stack();
    return 1;
}

static void mutate_slot(av_nvs *store, unsigned slot, int corrupt)
{
    const char *key = slot ? "s1" : "s0"; esp_err_t err;
    if (!store->opened || slot > 1) err = ESP_ERR_INVALID_ARG;
    else if (!corrupt) err = nvs_erase_key(store->handle, key);
    else {
        uint8_t blob[128]; size_t n = sizeof blob;
        err = nvs_get_blob(store->handle, key, blob, &n);
        if (err == ESP_OK && n) { blob[n - 1] ^= 0x80; err = nvs_set_blob(store->handle, key, blob, n); }
        else if (err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) err = nvs_commit(store->handle);
    store->last_error = err;
    store->last_operation = corrupt ? "nvs_get_blob/nvs_set_blob/nvs_commit" : "nvs_erase_key/nvs_commit";
    printf("%s,%c%u,esp_err=0x%x\n", corrupt ? "CORRUPT" : "DROP",
           store == &counter_store ? 'C' : 'S', slot, (unsigned)err);
}

static av_status start_object(av_object *o, av_nvs *store, const char *ns,
                              av_policy policy, uint8_t identity_byte)
{
    av_error e; av_status st;
    memset(o, 0, sizeof *o); o->tag = firmware_test_tag;
    memset(o->identity, identity_byte, sizeof o->identity);
    o->schema = 1; o->policy = policy; o->payload_limit = 8;
    if (av_nvs_open(store, "avtest", ns, NVS_READWRITE) != ESP_OK) return AV_IO;
    av_nvs_bind(store, o);
    if (policy == AV_COUNTER) {
        st = av_provision(o, counter_work, sizeof counter_work, NULL, 0, &e);
        if (st != AV_OK && st != AV_EXISTS) report("COUNTER_INIT", st, store, &e);
        return st;
    }
    {
        uint64_t current = 0; size_t current_length = 0;
        st = av_load(o, security_work, sizeof security_work, &current,
                     sizeof current, &current_length, &e);
    }
    if (st != AV_OK) report("SECURITY_LOAD_AT_BOOT", st, store, &e);
    return st;
}

static void run_write_batches(unsigned batches)
{
    uint64_t value = 0, first; size_t length = 0; unsigned b, i;
    av_error e; av_status st;
    st = av_load(&security_object, security_work, sizeof security_work,
                 &value, sizeof value, &length, &e);
    if (st != AV_OK || length != sizeof value) {
        report("STRESS_LOAD", st == AV_OK ? AV_BLOCKED : st, &security_store, &e);
        return;
    }
    for (b = 0; b < batches; ++b) {
        printf("BATCH_BEGIN,%u,%llu\n", b + 1, (unsigned long long)value);
        fflush(stdout);
        for (i = 0; i < 256; ++i) {
            if (value == UINT64_MAX) { puts("STRESS_STOP,reason=value_limit"); return; }
            ++value;
            st = av_store(&security_object, security_work, sizeof security_work,
                          &value, sizeof value, &e);
            if (st != AV_OK) { report("STRESS_STORE", st, &security_store, &e); return; }
            sample_stack();
        }
        st = av_reserve(&counter_object, counter_work, sizeof counter_work, 256, &first, &e);
        if (st != AV_OK) { report("STRESS_RESERVE", st, &counter_store, &e); return; }
        printf("RANGE,%llu,%llu\n", (unsigned long long)first,
               (unsigned long long)(first + 255));
        printf("BATCH_OK,%u,%llu\n", b + 1, (unsigned long long)value);
        fflush(stdout);
    }
    printf("STRESS_COMPLETE,%u,%llu\n", batches, (unsigned long long)value);
}

static void process(char *line)
{
    char *arg = strchr(line, ' '); uint64_t count, first; av_error e; av_status st;
    if (arg) *arg++ = '\0';
    if (process_extra(line, arg)) return;
    if (!strcmp(line, "R") && arg) {
        count = strtoull(arg, NULL, 10); first = UINT64_MAX;
        st = av_reserve(&counter_object, counter_work, sizeof counter_work, count, &first, &e);
        report("RESERVE", st, &counter_store, &e);
        if (st == AV_OK) printf("RANGE,%llu,%llu\n", (unsigned long long)first,
                                (unsigned long long)(first + count - 1));
    } else if (!strcmp(line, "S") && arg) {
        uint64_t v = strtoull(arg, NULL, 10);
        st = av_store(&security_object, security_work, sizeof security_work, &v, 8, &e);
        report("SECURITY_STORE", st, &security_store, &e);
    } else if (!strcmp(line, "L")) {
        uint64_t v = 0; size_t n = 0;
        st = av_load(&security_object, security_work, sizeof security_work, &v, 8, &n, &e);
        report("SECURITY_LOAD", st, &security_store, &e);
        if (st == AV_OK) printf("VALUE,%llu\n", (unsigned long long)v);
    } else if (!strcmp(line, "PROVISION_TEST")) {
        uint64_t initial = 0;
        st = av_provision(&security_object, security_work, sizeof security_work,
                          &initial, sizeof initial, &e);
        report("SECURITY_PROVISION_TEST_IDENTITY", st, &security_store, &e);
    } else if (!strcmp(line, "DROP_S0")) mutate_slot(&security_store, 0, 0);
    else if (!strcmp(line, "DROP_S1")) mutate_slot(&security_store, 1, 0);
    else if (!strcmp(line, "CORRUPT_S0")) mutate_slot(&security_store, 0, 1);
    else if (!strcmp(line, "CORRUPT_S1")) mutate_slot(&security_store, 1, 1);
    else if (!strcmp(line, "DROP_C0")) mutate_slot(&counter_store, 0, 0);
    else if (!strcmp(line, "DROP_C1")) mutate_slot(&counter_store, 1, 0);
    else if (!strcmp(line, "CORRUPT_C0")) mutate_slot(&counter_store, 0, 1);
    else if (!strcmp(line, "CORRUPT_C1")) mutate_slot(&counter_store, 1, 1);
    else if (!strcmp(line, "STATUS")) {
        printf("STATUS,mac=%s,reset_reason=%d,partition=avtest\n",
               device_mac, (int)startup_reset_reason);
    } else if (!strcmp(line, "STRESS") && arg) {
        unsigned batches = (unsigned)strtoul(arg, NULL, 10);
        if (!batches || batches > 100000u) puts("STRESS_INVALID,batches");
        else run_write_batches(batches);
    } else if (!strcmp(line, "PROVISION_COUNTER_TEST")) {
        st = av_provision(&counter_object, counter_work, sizeof counter_work, NULL, 0, &e);
        report("COUNTER_PROVISION_TEST_IDENTITY", st, &counter_store, &e);
    } else if (!strcmp(line, "FILL")) {
        uint8_t blob[96] = {0}; unsigned i; esp_err_t err = ESP_OK;
        for (i = 0; i < 1000 && err == ESP_OK; ++i) {
            char key[16]; snprintf(key, sizeof key, "f%014u", i);
            err = nvs_set_blob(counter_store.handle, key, blob, sizeof blob);
        }
        {
            esp_err_t commit_error = nvs_commit(counter_store.handle);
            if (err == ESP_OK) err = commit_error;
        }
        counter_store.last_error = err; counter_store.last_operation = "nvs_set_blob/fill";
        printf("FILL,esp_err=0x%x,count=%u\n", (unsigned)err, i);
    } else if (!strcmp(line, "REBOOT")) {
        fflush(stdout); esp_restart();
    } else {
        puts("ERR,commands=R <count>|S <value>|L|STATUS|STRESS <batches>|NOR_TEST|NOR_MOUNT|MODULES_TEST|MODULES_RECOVER|SEAL_PROVISION|SEAL_WRITE|SEAL_READ|SEAL_TAMPER|PAIR_KEYS PSK|PAIR_KEYS X25519|PAIR_BAD PSK|PAIR_BAD X25519|PAIR_ENC|PAIR_DEC <nonce> <cipher> <tag>|METRICS|PROVISION_TEST|PROVISION_COUNTER_TEST|DROP_S0|DROP_S1|CORRUPT_S0|CORRUPT_S1|DROP_C0|DROP_C1|CORRUPT_C0|CORRUPT_C1|FILL|REBOOT");
    }
}

void app_main(void)
{
    uint8_t mac[6]; char line[160]; esp_err_t err;
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    startup_reset_reason = esp_reset_reason();
    stack_free_boot = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    stack_free_min = stack_free_boot;
    err = nvs_flash_init_partition("avtest");
    if (err != ESP_OK) { printf("FATAL,avtest_init,0x%x\n", (unsigned)err); return; }
    if (start_object(&counter_object, &counter_store, "avctr", AV_COUNTER, 0xC1) == AV_BLOCKED)
        puts("COUNTER_BLOCKED,manual_test_recovery_required");
    if (start_object(&security_object, &security_store, "avsec", AV_STRICT, 0xA5) == AV_BLOCKED)
        puts("SECURITY_BLOCKED,manual_test_recovery_required");
    if (nor_setup() != 0) puts("NOR_SETUP,FAIL");
    else puts("NOR_SETUP,PASS,partition=avraw");
    if (seal_setup() != 0) puts("SEAL_SETUP,FAIL");
    else puts("SEAL_SETUP,PASS,counter_namespace=avnonce,data_namespace=avcipher");
    if (history_context_setup() != 0) puts("HISTORY_CONTEXT_SETUP,FAIL");
    else puts("HISTORY_CONTEXT_SETUP,PASS,object_ids=88+89");
    if (pair_setup() != 0) puts("PAIR_SETUP,FAIL");
    else puts("PAIR_SETUP,PASS,test_identities=COM19+COM33");
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_mac, sizeof device_mac, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    printf("READY,chip=ESP32-S3,mac=%s,partition=avtest,ns=avctr+avsec,reset_reason=%d\n",
           device_mac, (int)startup_reset_reason);
    {
        const esp_partition_t *raw = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               (esp_partition_subtype_t)0x40,
                                                               "avraw");
        printf("BOOT_METRICS,stack_free_bytes=%u,avraw_offset=0x%x,avraw_size=0x%x\n",
               (unsigned)stack_free_boot,
               raw ? (unsigned)raw->address : 0u, raw ? (unsigned)raw->size : 0u);
    }
    err = usb_serial_jtag_driver_install(&usb_cfg);
    if (err != ESP_OK) { printf("FATAL,usb_serial_jtag_driver_install,0x%x\n", (unsigned)err); return; }
    usb_serial_jtag_vfs_use_driver();
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line); while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
        process(line);
        sample_stack();
    }
}
