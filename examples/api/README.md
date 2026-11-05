# Context API examples

The caller keeps the context, rule table, backend routes, and workspace alive.
Use a production keyed tag callback. A backend must route each object ID to an
exclusive pair of slots and report success only after durable commit.

```c
static uint8_t work[2 * (AV_OVERHEAD + 8)];
static av_object_rule rules[] = {
    { 1, {0x11}, 1, AV_STRICT, 8 },   /* checkpoint */
    { 2, {0x22}, 1, AV_COUNTER, 8 }
};
static av_context context;
av_config config = { &backend, rules, 2, work, sizeof work,
                      product_tag, key_context };
av_error error;

if (av_open(&context, &config) != AV_OK) {
    /* Reject configuration; do not use context. */
}
```

Checkpoint objects are provisioned explicitly after application authorization.
The application decides which state transitions are legal:

```c
uint64_t checkpoint = 0, loaded;
size_t length;
/* Once, after authorization for this fresh identity: */
av_provision_id(&context, 1, &checkpoint, sizeof checkpoint, &error);
checkpoint = 42;
av_put(&context, 1, &checkpoint, sizeof checkpoint, &error);
av_get(&context, 1, &loaded, sizeof loaded, &length, &error);
```

Counters use a separate rule and the optional `aervault::counter` target. They
also require explicit provisioning; `av_counter_next` does not create a missing
object:

```c
uint64_t next;
av_provision_id(&context, 2, NULL, 0, &error); /* authorized initialization */
av_counter_next(&context, 2, &next, &error);   /* durable before return */
```

`av_open` rejects missing callbacks (including the required ID validator), an empty/duplicate rule set, unsupported
policies, a counter payload other than eight bytes, undersized workspace, and
IDs rejected by an optional backend validator. `av_put`/`av_get` return the
existing `av_status` values; write errors remain `AV_UNCERTAIN` with the
backend's native code in `av_error`.

For NVS, initialize one `av_nvs` handle per object namespace and use
`av_nvs_backend_bind(&router, &backend)`. It rejects duplicate IDs and duplicate
physical `(partition, namespace)` pairs even when those pairs were opened as
different handles; `av_open` then validates that every configured ID is routed.
