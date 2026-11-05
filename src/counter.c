#include "aervault_internal.h"
av_status av_reserve(const av_object *o, uint8_t *work, size_t n,
                     uint64_t count, uint64_t *first, av_error *e)
{
    size_t cap = av_internal_setup(o, work, n, e);
    av_slot s[2]; unsigned active; uint64_t previous, older; uint8_t encoded[8];
    av_status status;
    if (!cap || o->policy != AV_COUNTER || !count || !first) return AV_INVALID;
    status = av_internal_scan(o, work, cap, s, &active, e);
    if (status != AV_OK) return status;
    previous = av_internal_get(s[active].bytes + 36, 8);
    older = av_internal_get(s[active ^ 1u].bytes + 36, 8);
    if (previous < older) return AV_BLOCKED;
    if (UINT64_MAX - previous < count || s[active].revision == UINT64_MAX) return AV_LIMIT;
    av_internal_put(encoded, previous + count, 8);
    status = av_internal_write(o, work, s[active].revision + 1, encoded, 8, e);
    if (status == AV_OK) *first = previous + 1;
    return status;
}

av_status av_counter_next(av_context *context, uint32_t id,
                          uint64_t *next, av_error *e)
{
    av_context_binding binding;
    av_object object;
    av_status status = av_internal_context_object(context, id, &binding, &object);
    if (e) { e->stage = AV_STAGE_NONE; e->slot = 0; e->native_code = 0; }
    if (status != AV_OK) return status;
    if (object.policy != AV_COUNTER) return AV_INVALID;
    return av_reserve(&object, context->_work, context->_work_size, 1, next, e);
}
