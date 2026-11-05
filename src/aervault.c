#include "aervault.h"
#include "aervault_internal.h"
#include <string.h>

#define HEADER 36u
#define CONTEXT_STATE UINT32_C(0x41564358)



void av_internal_put(uint8_t *p, uint64_t value, unsigned width)
{
    unsigned i;
    for (i = 0; i < width; ++i) {
        p[i] = (uint8_t)value;
        value >>= 8;
    }
}

uint64_t av_internal_get(const uint8_t *p, unsigned width)
{
    uint64_t value = 0;
    while (width) value = (value << 8) | p[--width];
    return value;
}

static av_status failure(av_error *e, av_stage stage, unsigned id,
                         int code, av_status status)
{
    if (e) { e->stage = stage; e->slot = id; e->native_code = code; }
    return status;
}

size_t av_internal_setup(const av_object *o, uint8_t *work, size_t n, av_error *e)
{
    if (e) { e->stage = AV_STAGE_NONE; e->slot = 0; e->native_code = 0; }
    if (!o || !work || !o->read || !o->replace || !o->tag ||
        o->policy < AV_RECOVERABLE || o->policy > AV_COUNTER ||
        o->payload_limit > UINT32_MAX || n / 2 < AV_OVERHEAD ||
        o->payload_limit > n / 2 - AV_OVERHEAD ||
        (o->policy == AV_COUNTER && o->payload_limit != 8)) return 0;
    return AV_OVERHEAD + o->payload_limit;
}

static av_status read_slot(const av_object *o, av_slot *s, size_t cap,
                           unsigned id, av_error *e)
{
    uint8_t tag[AV_TAG_BYTES], difference = 0;
    uint8_t *p = s->bytes;
    size_t n = 0, i;
    int code = o->read(o->ctx, id, p, cap, &n);
    if (code == 1) return AV_MISSING;
    if (code) return failure(e, AV_STAGE_READ, id, code, AV_IO);
    if (n < AV_OVERHEAD || n > cap || memcmp(p, "AV01", 4) ||
        p[4] != (uint8_t)o->policy || p[5] || p[6] || p[7] ||
        memcmp(p + 8, o->identity, 16) || av_internal_get(p + 24, 4) != o->schema)
        return AV_BLOCKED;
    s->length = n - AV_OVERHEAD;
    s->revision = av_internal_get(p + 28, 8);
    if (!s->revision || (s->revision & 1u) != (id ^ 1u) ||
        (o->policy == AV_COUNTER && s->length != 8)) return AV_BLOCKED;
    code = o->tag(o->ctx, p, n - AV_TAG_BYTES, tag);
    if (code) return failure(e, AV_STAGE_TAG, id, code, AV_AUTH_ERROR);
    for (i = 0; i < AV_TAG_BYTES; ++i) difference |= tag[i] ^ p[n - AV_TAG_BYTES + i];
    return difference ? AV_BLOCKED : AV_OK;
}

av_status av_internal_scan(const av_object *o, uint8_t *work, size_t cap,
                      av_slot s[2], unsigned *active, av_error *e)
{
    unsigned i;
    for (i = 0; i < 2; ++i) {
        s[i].bytes = work + i * cap;
        s[i].status = read_slot(o, &s[i], cap, i, e);
        if (s[i].status == AV_IO || s[i].status == AV_AUTH_ERROR) return s[i].status;
    }
    if (s[0].status == AV_MISSING && s[1].status == AV_MISSING) return AV_MISSING;
    if (s[0].status != AV_OK || s[1].status != AV_OK) {
        if (o->policy != AV_RECOVERABLE ||
            (s[0].status != AV_OK && s[1].status != AV_OK)) return AV_BLOCKED;
        *active = s[0].status == AV_OK ? 0u : 1u;
        return AV_OK;
    }
    *active = s[0].revision > s[1].revision ? 0u : 1u;
    if (s[*active].revision - s[*active ^ 1u].revision != 1) return AV_BLOCKED;
    return AV_OK;
}

av_status av_internal_write(const av_object *o, uint8_t *p, uint64_t revision,
                            const void *value, size_t length, av_error *e)
{
    unsigned id = (unsigned)((revision & 1u) ^ 1u);
    int code;
    memcpy(p, "AV01", 4);
    p[4] = (uint8_t)o->policy;
    memset(p + 5, 0, 3);
    memcpy(p + 8, o->identity, 16);
    av_internal_put(p + 24, o->schema, 4);
    av_internal_put(p + 28, revision, 8);
    if (length) memcpy(p + HEADER, value, length);
    code = o->tag(o->ctx, p, HEADER + length, p + HEADER + length);
    if (code) return failure(e, AV_STAGE_TAG, id, code, AV_AUTH_ERROR);
    code = o->replace(o->ctx, id, p, AV_OVERHEAD + length);
    return code ? failure(e, AV_STAGE_REPLACE, id, code, AV_UNCERTAIN) : AV_OK;
}

av_status av_provision(const av_object *o, uint8_t *work, size_t n,
                       const void *value, size_t length, av_error *e)
{
    size_t cap = av_internal_setup(o, work, n, e);
    uint8_t zero[8] = {0};
    av_slot s[2];
    unsigned active;
    av_status status;
    if (!cap || length > o->payload_limit || (length && !value)) return AV_INVALID;
    if (o->policy == AV_COUNTER) {
        if (length) return AV_INVALID;
        value = zero;
        length = sizeof zero;
    }
    status = av_internal_scan(o, work, cap, s, &active, e);
    if (status != AV_MISSING) return status == AV_OK ? AV_EXISTS : status;
    status = av_internal_write(o, work, 1, value, length, e);
    if (status != AV_OK) return status;
    status = av_internal_write(o, work, 2, value, length, e);
    return status == AV_AUTH_ERROR ? AV_UNCERTAIN : status;
}

av_status av_load(const av_object *o, uint8_t *work, size_t n,
                  void *value, size_t capacity, size_t *length, av_error *e)
{
    size_t cap = av_internal_setup(o, work, n, e);
    av_slot s[2];
    unsigned active;
    av_status status;
    if (!cap || !length || (!value && capacity)) return AV_INVALID;
    status = av_internal_scan(o, work, cap, s, &active, e);
    if (status == AV_MISSING && o->policy == AV_STRICT) return AV_BLOCKED;
    if (status != AV_OK) return status;
    if (s[active].length > capacity) return AV_LIMIT;
    if (s[active].length) memcpy(value, s[active].bytes + HEADER, s[active].length);
    *length = s[active].length;
    return AV_OK;
}

av_status av_store(const av_object *o, uint8_t *work, size_t n,
                   const void *value, size_t length, av_error *e)
{
    size_t cap = av_internal_setup(o, work, n, e);
    av_slot s[2];
    unsigned active;
    av_status status;
    if (!cap || o->policy == AV_COUNTER || length > o->payload_limit ||
        (length && !value)) return AV_INVALID;
    status = av_internal_scan(o, work, cap, s, &active, e);
    if (status != AV_OK) return status;
    if (s[active].revision == UINT64_MAX) return AV_LIMIT;
    return av_internal_write(o, work, s[active].revision + 1, value, length, e);
}

static int context_read(void *ctx, unsigned slot, uint8_t *out,
                        size_t capacity, size_t *length)
{
    av_context_binding *b = (av_context_binding *)ctx;
    const av_context *c = b->context;
    return c->_backend.read(c->_backend.ctx, b->id, slot, out, capacity, length);
}

static int context_replace(void *ctx, unsigned slot, const uint8_t *data,
                           size_t length)
{
    av_context_binding *b = (av_context_binding *)ctx;
    const av_context *c = b->context;
    return c->_backend.replace(c->_backend.ctx, b->id, slot, data, length);
}

static int context_tag(void *ctx, const uint8_t *data, size_t length,
                      uint8_t out[AV_TAG_BYTES])
{
    av_context_binding *b = (av_context_binding *)ctx;
    const av_context *c = b->context;
    return c->_tag(c->_tag_ctx, data, length, out);
}

static void clear_error(av_error *e)
{
    if (e) { e->stage = AV_STAGE_NONE; e->slot = 0; e->native_code = 0; }
}

av_status av_open(av_context *context, const av_config *config)
{
    size_t i, j, half;
    if (!context) return AV_INVALID;
    memset(context, 0, sizeof *context);
    if (!config || !config->backend || !config->backend->read ||
        !config->backend->replace || !config->backend->validate_id ||
        !config->tag || !config->objects ||
        !config->object_count || !config->work) return AV_INVALID;
    half = config->work_size / 2;
    if (half < AV_OVERHEAD) return AV_INVALID;
    for (i = 0; i < config->object_count; ++i) {
        const av_object_rule *r = &config->objects[i];
        if (r->policy < AV_RECOVERABLE || r->policy > AV_COUNTER ||
            r->payload_limit > UINT32_MAX ||
            r->payload_limit > half - AV_OVERHEAD ||
            (r->policy == AV_COUNTER && r->payload_limit != 8))
            return AV_INVALID;
        for (j = 0; j < i; ++j)
            if (config->objects[j].id == r->id) return AV_INVALID;
        if (!config->backend->validate_id(config->backend->ctx, r->id))
            return AV_INVALID;
    }
    context->_backend = *config->backend;
    context->_objects = config->objects;
    context->_object_count = config->object_count;
    context->_work = config->work;
    context->_work_size = config->work_size;
    context->_tag = config->tag;
    context->_tag_ctx = config->tag_ctx;
    context->_state = CONTEXT_STATE;
    return AV_OK;
}

av_status av_internal_context_object(const av_context *context, uint32_t id,
                                     av_context_binding *binding, av_object *object)
{
    size_t i;
    if (!context || context->_state != CONTEXT_STATE || !binding || !object)
        return AV_INVALID;
    for (i = 0; i < context->_object_count; ++i) {
        const av_object_rule *r = &context->_objects[i];
        if (r->id == id) {
            binding->context = context;
            binding->id = id;
            memset(object, 0, sizeof *object);
            object->ctx = binding;
            object->read = context_read;
            object->replace = context_replace;
            object->tag = context_tag;
            memcpy(object->identity, r->identity, sizeof object->identity);
            object->schema = r->schema;
            object->policy = r->policy;
            object->payload_limit = r->payload_limit;
            return AV_OK;
        }
    }
    return AV_INVALID;
}

av_status av_provision_id(av_context *context, uint32_t id,
                          const void *value, size_t length, av_error *e)
{
    av_context_binding binding;
    av_object object;
    av_status status;
    clear_error(e);
    status = av_internal_context_object(context, id, &binding, &object);
    if (status != AV_OK) return status;
    return av_provision(&object, context->_work, context->_work_size,
                        value, length, e);
}

av_status av_put(av_context *context, uint32_t id,
                 const void *value, size_t length, av_error *e)
{
    av_context_binding binding;
    av_object object;
    av_status status;
    clear_error(e);
    status = av_internal_context_object(context, id, &binding, &object);
    if (status != AV_OK) return status;
    return av_store(&object, context->_work, context->_work_size,
                    value, length, e);
}

av_status av_get(av_context *context, uint32_t id,
                 void *value, size_t capacity, size_t *length, av_error *e)
{
    av_context_binding binding;
    av_object object;
    av_status status;
    clear_error(e);
    status = av_internal_context_object(context, id, &binding, &object);
    if (status != AV_OK) return status;
    return av_load(&object, context->_work, context->_work_size,
                   value, capacity, length, e);
}

