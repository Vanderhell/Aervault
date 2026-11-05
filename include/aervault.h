#ifndef AERVAULT_H
#define AERVAULT_H
#include <stddef.h>
#include <stdint.h>

#define AV_OVERHEAD 52u
#define AV_TAG_BYTES 16u

typedef enum { AV_OK, AV_INVALID, AV_MISSING, AV_IO, AV_BLOCKED,
               AV_UNCERTAIN, AV_EXISTS, AV_LIMIT, AV_AUTH_ERROR } av_status;
typedef enum { AV_RECOVERABLE = 1, AV_STRICT, AV_COUNTER } av_policy;

/* Read returns 0 on success, 1 only for absent, any other code for I/O failure.
 * Set *length to the full stored length on success. Never overrun capacity.
 * Replace returns 0 only after durable replacement. A failed replacement
 * may already have reached storage. Neither operation may damage the peer slot.
 * Callbacks share ctx. The configured pair of slots is exclusive to this object.
 */
typedef struct {
    void *ctx;
    int (*read)(void *, unsigned, uint8_t *, size_t, size_t *);
    int (*replace)(void *, unsigned, const uint8_t *, size_t);
    /* Compute a keyed tag over all supplied bytes. Return 0 on success. */
    int (*tag)(void *, const uint8_t *, size_t, uint8_t [AV_TAG_BYTES]);
    uint8_t identity[16];
    uint32_t schema;
    av_policy policy;
    size_t payload_limit;
} av_object;

/* Context API: one backend and workspace, with immutable rules keyed by ID. */
typedef int (*av_id_read_fn)(void *, uint32_t, unsigned, uint8_t *, size_t, size_t *);
typedef int (*av_id_replace_fn)(void *, uint32_t, unsigned, const uint8_t *, size_t);
typedef int (*av_id_validate_fn)(void *, uint32_t);
typedef int (*av_tag_fn)(void *, const uint8_t *, size_t, uint8_t [AV_TAG_BYTES]);

typedef struct {
    void *ctx;
    av_id_read_fn read;
    av_id_replace_fn replace;
    /* Required: nonzero when an ID is mapped and ready to use. */
    av_id_validate_fn validate_id;
} av_backend;

typedef struct {
    uint32_t id;
    uint8_t identity[16];
    uint32_t schema;
    av_policy policy;
    size_t payload_limit;
} av_object_rule;

typedef struct {
    const av_backend *backend;
    const av_object_rule *objects;
    size_t object_count;
    uint8_t *work;
    size_t work_size;
    av_tag_fn tag;
    void *tag_ctx;
} av_config;

/* Caller owns context, work and object rules for their documented lifetimes.
 * Serialize operations using a context. Backend must isolate IDs and slots. */
typedef struct {
    uint32_t _state;
    av_backend _backend;
    const av_object_rule *_objects;
    size_t _object_count;
    uint8_t *_work;
    size_t _work_size;
    av_tag_fn _tag;
    void *_tag_ctx;
} av_context;

typedef enum { AV_STAGE_NONE, AV_STAGE_READ, AV_STAGE_REPLACE, AV_STAGE_TAG } av_stage;
typedef struct { av_stage stage; unsigned slot; int native_code; } av_error;

/* Caller-owned workspace of 2 * (AV_OVERHEAD + payload_limit) bytes.
 * Serialize all calls per object. Inputs/outputs must not overlap workspace.
 * Provision only with external authorization for a genuinely fresh identity.
 * Missing media alone is insufficient authorization to provision.
 */
av_status av_provision(const av_object *, uint8_t *, size_t,
                       const void *, size_t, av_error *);
av_status av_load(const av_object *, uint8_t *, size_t,
                  void *, size_t, size_t *, av_error *);
av_status av_store(const av_object *, uint8_t *, size_t,
                   const void *, size_t, av_error *);
/* Reserve count consecutive values and return the first after durable commit. */
av_status av_reserve(const av_object *, uint8_t *, size_t,
                     uint64_t, uint64_t *, av_error *);

/* Open validates and snapshots backend/workspace configuration once. */
av_status av_open(av_context *, const av_config *);
/* Explicit provisioning is separate; missing media does not authorize it. */
av_status av_provision_id(av_context *, uint32_t, const void *, size_t, av_error *);
av_status av_put(av_context *, uint32_t, const void *, size_t, av_error *);
av_status av_get(av_context *, uint32_t, void *, size_t, size_t *, av_error *);
/* Defined by the optional aervault::counter target. */
av_status av_counter_next(av_context *, uint32_t, uint64_t *, av_error *);
#endif
