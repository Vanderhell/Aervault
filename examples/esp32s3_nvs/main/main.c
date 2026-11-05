#include "aervault.h"
#include "aervault_nvs.h"
#include "esp_mac.h"
#include "esp_system.h"
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
        puts("ERR,commands=R <count>|S <value>|L|STATUS|STRESS <batches>|PROVISION_TEST|PROVISION_COUNTER_TEST|DROP_S0|DROP_S1|CORRUPT_S0|CORRUPT_S1|DROP_C0|DROP_C1|CORRUPT_C0|CORRUPT_C1|FILL|REBOOT");
    }
}

void app_main(void)
{
    uint8_t mac[6]; char line[64]; esp_err_t err;
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    startup_reset_reason = esp_reset_reason();
    err = nvs_flash_init_partition("avtest");
    if (err != ESP_OK) { printf("FATAL,avtest_init,0x%x\n", (unsigned)err); return; }
    if (start_object(&counter_object, &counter_store, "avctr", AV_COUNTER, 0xC1) == AV_BLOCKED)
        puts("COUNTER_BLOCKED,manual_test_recovery_required");
    if (start_object(&security_object, &security_store, "avsec", AV_STRICT, 0xA5) == AV_BLOCKED)
        puts("SECURITY_BLOCKED,manual_test_recovery_required");
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_mac, sizeof device_mac, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    printf("READY,chip=ESP32-S3,mac=%s,partition=avtest,ns=avctr+avsec,reset_reason=%d\n",
           device_mac, (int)startup_reset_reason);
    err = usb_serial_jtag_driver_install(&usb_cfg);
    if (err != ESP_OK) { printf("FATAL,usb_serial_jtag_driver_install,0x%x\n", (unsigned)err); return; }
    usb_serial_jtag_vfs_use_driver();
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line); while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
        process(line);
    }
}
