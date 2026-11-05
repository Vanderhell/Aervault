#include "aervault.h"
#include "ram_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct {
    uint8_t bytes[2][64];
    size_t length[2];
    unsigned writes, fail_at;
    int mode, read_failure, tag_failure;
} media;

typedef struct { uint32_t id; media value; } id_entry;
typedef struct { id_entry entries[2]; unsigned count; } id_media;

static int read_record(void *, unsigned, uint8_t *, size_t, size_t *);
static int replace_record(void *, unsigned, const uint8_t *, size_t);

static media *id_find(id_media *m, uint32_t id)
{
    unsigned i;
    for (i = 0; i < m->count; ++i)
        if (m->entries[i].id == id) return &m->entries[i].value;
    return NULL;
}

static int id_read(void *ctx, uint32_t id, unsigned slot, uint8_t *out,
                   size_t cap, size_t *length)
{
    media *m = id_find((id_media *)ctx, id);
    return m ? read_record(m, slot, out, cap, length) : -77;
}

static int id_replace(void *ctx, uint32_t id, unsigned slot,
                      const uint8_t *data, size_t length)
{
    media *m = id_find((id_media *)ctx, id);
    return m ? replace_record(m, slot, data, length) : -77;
}

static int id_validate(void *ctx, uint32_t id)
{
    return id_find((id_media *)ctx, id) != NULL;
}

static int read_record(void *p, unsigned id, uint8_t *out, size_t cap, size_t *n)
{
    media *m = p;
    if (m->read_failure) return m->read_failure;
    *n = m->length[id];
    if (!*n) return 1;
    if (*n <= cap) memcpy(out, m->bytes[id], *n);
    return 0;
}

static int replace_record(void *p, unsigned id, const uint8_t *in, size_t n)
{
    media *m = p;
    int mode = ++m->writes == m->fail_at ? m->mode : 0;
    if (mode == 1) return -33;
    if (mode == 3) n /= 2;
    memcpy(m->bytes[id], in, n);
    m->length[id] = n;
    if (mode == 4) return -44; /* durable bytes, simulated reset before result */
    return mode ? -33 : 0;     /* mode 2 models a failed commit */
}

/* Test double only. Intentionally not a cryptographic MAC. */
static int test_tag(void *p, const uint8_t *in, size_t n, uint8_t *out)
{
    media *m = p;
    uint32_t h = UINT32_C(2166136261);
    unsigned i;
    if (m->tag_failure) return m->tag_failure;
    while (n--) h = (h ^ *in++) * UINT32_C(16777619);
    for (i = 0; i < AV_TAG_BYTES; ++i)
        out[i] = (uint8_t)(h >> (8 * (i % 4)));
    return 0;
}

static av_object object(media *m, av_policy policy)
{
    av_object o = {m, read_record, replace_record, test_tag, {1}, 42, policy, 8};
    return o;
}

static void ram_backend_smoke(void)
{
    av_ram_backend m; uint8_t slot0[64], slot1[64], output[64]; size_t n = 0;
    av_ram_init(&m, slot0, slot1, sizeof slot0);
    CHECK(av_ram_replace(&m, 0, (const uint8_t *)"persist", 7) == 0);
    CHECK(av_ram_read(&m, 0, output, sizeof output, &n) == 0);
    CHECK(n == 7 && memcmp(output, "persist", 7) == 0);
    m.fail_at = 2; m.mode = 1;
    CHECK(av_ram_replace(&m, 1, (const uint8_t *)"no", 2) == -33 && m.length[1] == 0);
    m.fail_at = 3; m.mode = 3;
    CHECK(av_ram_replace(&m, 1, (const uint8_t *)"part", 4) == -33 && m.length[1] == 2);
    m.fail_at = 4; m.mode = 2;
    CHECK(av_ram_replace(&m, 1, (const uint8_t *)"full", 4) == -33 && m.length[1] == 4);
}

static void counter_faults(void)
{
    uint8_t boot[120], restart[120];
    int mode;
    for (mode = 1; mode <= 4; ++mode) {
        media m = {0};
        av_object o = object(&m, AV_COUNTER);
        av_error e;
        uint64_t first;
        CHECK(av_provision(&o, boot, sizeof boot, NULL, 0, &e) == AV_OK);
        CHECK(av_reserve(&o, boot, sizeof boot, 3, &first, &e) == AV_OK && first == 1);
        m.fail_at = 4;
        m.mode = mode;
        first = 99;
        CHECK(av_reserve(&o, boot, sizeof boot, 5, &first, &e) == AV_UNCERTAIN);
        CHECK(first == 99 && e.stage == AV_STAGE_REPLACE &&
              e.native_code == (mode == 4 ? -44 : -33));
        o = object(&m, AV_COUNTER);
        if (mode == 3) {
            CHECK(av_reserve(&o, restart, sizeof restart, 1, &first, &e) == AV_BLOCKED);
            CHECK(first == 99);
        } else {
            CHECK(av_reserve(&o, restart, sizeof restart, 1, &first, &e) == AV_OK);
            CHECK(first == (mode == 1 ? 4u : 9u));
        }
    }
}

static void provisioning_restarts(void)
{
    unsigned failure_point, mode;
    for (failure_point = 1; failure_point <= 2; ++failure_point) {
        for (mode = 1; mode <= 4; ++mode) {
            media m = {0}; uint8_t work[120], reboot_work[120];
            av_object o = object(&m, AV_COUNTER); uint64_t first = 0xfeed;
            m.fail_at = failure_point; m.mode = (int)mode;
            CHECK(av_provision(&o, work, sizeof work, NULL, 0, NULL) == AV_UNCERTAIN);
            o = object(&m, AV_COUNTER);
            if (failure_point == 2 && (mode == 2 || mode == 4)) {
                CHECK(av_reserve(&o, reboot_work, sizeof reboot_work, 1, &first, NULL) == AV_OK);
                CHECK(first == 1);
            } else {
                av_status expected = failure_point == 1 && mode == 1 ? AV_MISSING : AV_BLOCKED;
                CHECK(av_reserve(&o, reboot_work, sizeof reboot_work, 1, &first, NULL) == expected);
                CHECK(first == 0xfeed);
            }
        }
    }
}

static void strict_provisioning_restarts(void)
{
    unsigned failure_point, mode;
    for (failure_point = 1; failure_point <= 2; ++failure_point) {
        for (mode = 1; mode <= 4; ++mode) {
            media m = {0};
            uint8_t work[120], reboot_work[120], out[4] = {0};
            size_t length = 0;
            av_object o = object(&m, AV_STRICT);
            av_status expected;
            memset(work, 0xA5, sizeof work);
            m.fail_at = failure_point;
            m.mode = (int)mode;
            CHECK(av_provision(&o, work, sizeof work, "safe", 4, NULL) == AV_UNCERTAIN);
            memset(reboot_work, 0x5A, sizeof reboot_work);
            o = object(&m, AV_STRICT);
            expected = failure_point == 2 && (mode == 2 || mode == 4) ? AV_OK : AV_BLOCKED;
            CHECK(av_load(&o, reboot_work, sizeof reboot_work, out, sizeof out,
                          &length, NULL) == expected);
            if (expected == AV_OK)
                CHECK(length == 4 && memcmp(out, "safe", 4) == 0);
            else
                CHECK(length == 0);
        }
    }
}

static void store_restarts(void)
{
    unsigned mode;
    for (mode = 1; mode <= 4; ++mode) {
        media m = {0}; uint8_t work[120], reboot_work[120], out[3] = {0};
        size_t length = 0; av_object o = object(&m, AV_STRICT);
        CHECK(av_provision(&o, work, sizeof work, "old", 3, NULL) == AV_OK);
        m.fail_at = m.writes + 1; m.mode = (int)mode;
        CHECK(av_store(&o, work, sizeof work, "new", 3, NULL) == AV_UNCERTAIN);
        o = object(&m, AV_STRICT);
        if (mode == 1) {
            CHECK(av_load(&o, reboot_work, sizeof reboot_work, out, sizeof out, &length, NULL) == AV_OK);
            CHECK(length == 3 && memcmp(&out, "old", 3) == 0);
        } else if (mode == 2 || mode == 4) {
            CHECK(av_load(&o, reboot_work, sizeof reboot_work, out, sizeof out, &length, NULL) == AV_OK);
            CHECK(length == 3 && memcmp(out, "new", 3) == 0);
        } else {
            CHECK(av_load(&o, reboot_work, sizeof reboot_work, out, sizeof out, &length, NULL) == AV_BLOCKED);
        }
    }
}

static void boot_and_bounds(void)
{
    uint8_t work[120];
    unsigned point;
    for (point = 1; point <= 2; ++point) {
        media m = {0};
        av_object o = object(&m, AV_COUNTER);
        uint64_t first = 0;
        m.fail_at = point;
        m.mode = 1;
        CHECK(av_provision(&o, work, sizeof work, NULL, 0, NULL) == AV_UNCERTAIN);
        CHECK(av_reserve(&o, work, sizeof work, 1, &first, NULL) ==
              (point == 1 ? AV_MISSING : AV_BLOCKED));
        CHECK(first == 0);
    }
    {
        media m = {0};
        av_object o = object(&m, AV_COUNTER);
        uint64_t first;
        CHECK(av_provision(&o, work, sizeof work - 1, NULL, 0, NULL) == AV_INVALID);
        CHECK(av_provision(&o, work, sizeof work, NULL, 0, NULL) == AV_OK);
        CHECK(av_provision(&o, work, sizeof work, NULL, 0, NULL) == AV_EXISTS);
        first = 0xfeed;
        CHECK(av_reserve(&o, work, sizeof work, 0, &first, NULL) == AV_INVALID);
        CHECK(first == 0xfeed);
        CHECK(av_reserve(&o, work, sizeof work, 1, NULL, NULL) == AV_INVALID);
        CHECK(av_reserve(&o, work, sizeof work, UINT64_MAX, &first, NULL) == AV_OK && first == 1);
        first = 0xfeed;
        CHECK(av_reserve(&o, work, sizeof work, 1, &first, NULL) == AV_LIMIT);
        CHECK(first == 0xfeed);
        m.length[0] = 0;
        CHECK(av_reserve(&o, work, sizeof work, 1, &first, NULL) == AV_BLOCKED);
    }
    {
        media m = {0};
        av_object o = object(&m, AV_COUNTER);
        uint64_t first = 0;
        CHECK(av_provision(&o, work, sizeof work, NULL, 0, NULL) == AV_OK);
        CHECK(av_reserve(&o, work, sizeof work, UINT64_MAX - 1, &first, NULL) == AV_OK);
        CHECK(first == 1);
        CHECK(av_reserve(&o, work, sizeof work, 1, &first, NULL) == AV_OK);
        CHECK(first == UINT64_MAX);
        first = 0xfeed;
        CHECK(av_reserve(&o, work, sizeof work, 1, &first, NULL) == AV_LIMIT);
        CHECK(first == 0xfeed);
    }
}

static void corruption_matrix(void)
{
    unsigned damaged_slot;
    for (damaged_slot = 0; damaged_slot < 2; ++damaged_slot) {
        int policy;
        for (policy = AV_RECOVERABLE; policy <= AV_STRICT; ++policy) {
            media m = {0};
            uint8_t work[120], reboot_work[120], out = 0x7E;
            size_t length = 0;
            av_object o = object(&m, (av_policy)policy);
            CHECK(av_provision(&o, work, sizeof work, "A", 1, NULL) == AV_OK);
            CHECK(av_store(&o, work, sizeof work, "B", 1, NULL) == AV_OK);
            m.bytes[damaged_slot][m.length[damaged_slot] - 1] ^= 1;
            memset(reboot_work, 0xC3, sizeof reboot_work);
            if (policy == AV_RECOVERABLE) {
                av_status st;
                st = av_load(&o, reboot_work, sizeof reboot_work, &out, 1,
                             &length, NULL);
                CHECK(st == AV_OK);
                CHECK(length == 1 && out == (damaged_slot == 0 ? 'A' : 'B'));
            } else {
                CHECK(av_load(&o, reboot_work, sizeof reboot_work, &out, 1,
                              &length, NULL) == AV_BLOCKED);
                CHECK(out == 0x7E && length == 0);
            }
        }
    }
    {
        media m = {0};
        uint8_t work[120], reboot_work[120], out = 0x55;
        size_t length = 0;
        av_object o = object(&m, AV_RECOVERABLE);
        CHECK(av_provision(&o, work, sizeof work, "A", 1, NULL) == AV_OK);
        CHECK(av_store(&o, work, sizeof work, "B", 1, NULL) == AV_OK);
        m.bytes[0][0] ^= 1; /* invalid magic in older record */
        m.bytes[1][m.length[1] - 1] ^= 1; /* invalid tag in newest record */
        memset(reboot_work, 0x3C, sizeof reboot_work);
        CHECK(av_load(&o, reboot_work, sizeof reboot_work, &out, 1,
                      &length, NULL) == AV_BLOCKED);
        CHECK(out == 0x55 && length == 0);
    }
    {
        media m = {0};
        uint8_t work[120], reboot_work[120], out = 0;
        size_t length = 0;
        av_object o = object(&m, AV_STRICT);
        CHECK(av_provision(&o, work, sizeof work, "A", 1, NULL) == AV_OK);
        CHECK(av_store(&o, work, sizeof work, "B", 1, NULL) == AV_OK);
        m.length[1] = AV_OVERHEAD - 1; /* truncated record */
        memset(reboot_work, 0x96, sizeof reboot_work);
        CHECK(av_load(&o, reboot_work, sizeof reboot_work, &out, 1,
                      &length, NULL) == AV_BLOCKED);
        CHECK(length == 0);
    }
}

static void context_api(void)
{
    id_media storage = {0};
    av_backend backend = {&storage, id_read, id_replace, id_validate};
    av_object_rule rules[2] = {
        {10, {0x10}, 1, AV_STRICT, 8},
        {20, {0x20}, 1, AV_COUNTER, 8}
    };
    uint8_t work[120], out[8] = {0};
    size_t length = 0;
    uint64_t next = UINT64_C(0xfeed);
    av_config config = {&backend, rules, 2, work, sizeof work, test_tag,
                        &storage.entries[0].value};
    av_context context;
    av_error e;

    storage.count = 2;
    storage.entries[0].id = 10;
    storage.entries[1].id = 20;
    memset(work, 0xA5, sizeof work);

    CHECK(av_open(&context, NULL) == AV_INVALID);
    CHECK(av_open(&context, &config) == AV_OK);
    CHECK(av_get(&context, 10, out, sizeof out, &length, &e) == AV_BLOCKED);
    CHECK(av_put(&context, 10, "before", 6, &e) == AV_MISSING);
    CHECK(av_counter_next(&context, 20, &next, &e) == AV_MISSING);
    CHECK(next == UINT64_C(0xfeed));

    CHECK(av_provision_id(&context, 10, "initial", 7, &e) == AV_OK);
    CHECK(av_provision_id(&context, 10, "again", 5, &e) == AV_EXISTS);
    CHECK(av_get(&context, 10, out, sizeof out, &length, &e) == AV_OK);
    CHECK(length == 7 && memcmp(out, "initial", 7) == 0);
    CHECK(av_put(&context, 10, "checkpt!", 8, &e) == AV_OK);
    memset(out, 0, sizeof out);
    CHECK(av_get(&context, 10, out, sizeof out, &length, &e) == AV_OK);
    CHECK(length == 8 && memcmp(out, "checkpt!", 8) == 0);
    CHECK(av_put(&context, 20, "x", 1, &e) == AV_INVALID);
    CHECK(av_counter_next(&context, 10, &next, &e) == AV_INVALID);
    CHECK(av_get(&context, 99, out, sizeof out, &length, &e) == AV_INVALID);

    CHECK(av_provision_id(&context, 20, NULL, 0, &e) == AV_OK);
    CHECK(av_counter_next(&context, 20, &next, &e) == AV_OK && next == 1);
    CHECK(av_counter_next(&context, 20, &next, &e) == AV_OK && next == 2);

    storage.entries[0].value.read_failure = -91;
    CHECK(av_get(&context, 10, out, sizeof out, &length, &e) == AV_IO);
    CHECK(e.stage == AV_STAGE_READ && e.native_code == -91);
    storage.entries[0].value.read_failure = 0;
    storage.entries[0].value.fail_at = storage.entries[0].value.writes + 1;
    storage.entries[0].value.mode = 1;
    CHECK(av_put(&context, 10, "uncert!", 7, &e) == AV_UNCERTAIN);
    CHECK(e.stage == AV_STAGE_REPLACE && e.native_code == -33);

    /* Open rejects invalid policies, undersized buffers, duplicate IDs, and
     * object IDs the selected backend cannot route. */
    rules[1].payload_limit = 7;
    CHECK(av_open(&context, &config) == AV_INVALID);
    rules[1].payload_limit = 8;
    rules[1].id = rules[0].id;
    CHECK(av_open(&context, &config) == AV_INVALID);
    rules[1].id = 30;
    CHECK(av_open(&context, &config) == AV_INVALID);
    rules[1].id = 20;
    config.work_size = sizeof work - 1;
    CHECK(av_open(&context, &config) == AV_INVALID);
    config.work_size = sizeof work;
    rules[1].payload_limit = 9;
    CHECK(av_open(&context, &config) == AV_INVALID);
    rules[1].payload_limit = 8;
    rules[1].policy = (av_policy)0;
    CHECK(av_open(&context, &config) == AV_INVALID);
    rules[1].policy = AV_COUNTER;
    config.object_count = 0;
    CHECK(av_open(&context, &config) == AV_INVALID);
    config.object_count = 2;
    backend.replace = NULL;
    CHECK(av_open(&context, &config) == AV_INVALID);
    backend.replace = id_replace;
    backend.validate_id = NULL;
    CHECK(av_open(&context, &config) == AV_INVALID);
    backend.validate_id = id_validate;
    CHECK(av_get(&context, 10, out, sizeof out, &length, &e) == AV_INVALID);
}

static void recovery(void)
{
    uint8_t work[120], out = 0;
    size_t length = 0;
    int mode;
    for (mode = AV_RECOVERABLE; mode <= AV_STRICT; ++mode) {
        media m = {0};
        av_error e;
        av_object o = object(&m, (av_policy)mode);
        if (mode == AV_STRICT)
            CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_BLOCKED);
        CHECK(av_provision(&o, work, sizeof work, "A", 1, NULL) == AV_OK);
        CHECK(av_store(&o, work, sizeof work, "B", 1, NULL) == AV_OK);
        CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_OK);
        CHECK(length == 1 && out == 'B');
        m.bytes[0][36] ^= 1;
        out = 0;
        if (mode == AV_RECOVERABLE) {
            CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_OK);
            CHECK(out == 'A');
            CHECK(av_store(&o, work, sizeof work, "C", 1, NULL) == AV_OK);
            CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_OK && out == 'C');
        } else {
            CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_BLOCKED);
            CHECK(out == 0);
        }
        m.read_failure = -91;
        CHECK(av_load(&o, work, sizeof work, &out, 1, &length, &e) == AV_IO);
        CHECK(e.stage == AV_STAGE_READ && e.native_code == -91);
        m.read_failure = 0;
        m.tag_failure = -92;
        CHECK(av_load(&o, work, sizeof work, &out, 1, &length, &e) == AV_AUTH_ERROR);
        CHECK(e.stage == AV_STAGE_TAG && e.native_code == -92);
        m.tag_failure = 0;
        o.identity[0] = 2;
        CHECK(av_load(&o, work, sizeof work, &out, 1, &length, NULL) == AV_BLOCKED);
    }
}

int main(void)
{
    ram_backend_smoke();
    counter_faults();
    provisioning_restarts();
    strict_provisioning_restarts();
    store_restarts();
    boot_and_bounds();
    corruption_matrix();
    context_api();
    recovery();
    return 0;
}
