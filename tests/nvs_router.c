#include "aervault.h"
#include "aervault_nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int tag(void *ctx, const uint8_t *data, size_t size,
               uint8_t out[AV_TAG_BYTES])
{
    size_t i; (void)ctx;
    for (i = 0; i < AV_TAG_BYTES; ++i) out[i] = (uint8_t)(size + data[i % size]);
    return 0;
}

static void configure(av_context *context, av_nvs_router *router,
                      av_nvs_route *routes, av_backend *backend,
                      av_object_rule *rules, uint8_t work[120])
{
    av_config config;
    router->routes = routes; router->count = 2;
    backend->ctx = router;
    CHECK(av_nvs_backend_bind(router, backend) == ESP_OK);
    memset(rules, 0, 2 * sizeof *rules);
    rules[0].id = routes[0].object_id;
    rules[0].identity[0] = 1; rules[0].schema = 1;
    rules[0].policy = AV_STRICT; rules[0].payload_limit = 8;
    rules[1].id = routes[1].object_id;
    rules[1].identity[0] = 2; rules[1].schema = 1;
    rules[1].policy = AV_STRICT; rules[1].payload_limit = 8;
    config.backend = backend; config.objects = rules; config.object_count = 2;
    config.work = work; config.work_size = 120; config.tag = tag; config.tag_ctx = NULL;
    CHECK(av_open(context, &config) == AV_OK);
}

int main(void)
{
    av_nvs a, b;
    av_nvs_route routes[2];
    av_nvs_router router;
    av_backend backend;
    av_object_rule rules[2] = {{0}};
    av_context context;
    uint8_t work[120];
    char long_namespace[17] = "1234567890123456";

    CHECK(av_nvs_open(&a, NULL, "same", NVS_READWRITE) == ESP_OK);
    CHECK(av_nvs_open(&b, "nvs", "same", NVS_READWRITE) == ESP_OK);
    CHECK(strcmp(a.partition, "nvs") == 0);
    routes[0].object_id = 1; routes[0].store = &a;
    routes[1].object_id = 2; routes[1].store = &b;
    router.routes = routes; router.count = 2;
    CHECK(!av_nvs_backend_validate_id(&router, 1));
    CHECK(!av_nvs_backend_validate_id(&router, 2));
    CHECK(av_nvs_backend_bind(&router, &backend) == ESP_ERR_INVALID_ARG);
    {
        av_config config = {&backend, rules, 2, work, sizeof work, tag, NULL};
        backend.ctx = &router; backend.read = av_nvs_backend_read;
        backend.replace = av_nvs_backend_replace;
        backend.validate_id = av_nvs_backend_validate_id;
        rules[0].id = 1; rules[0].identity[0] = 1; rules[0].schema = 1;
        rules[0].policy = AV_STRICT; rules[0].payload_limit = 8;
        rules[1].id = 2; rules[1].identity[0] = 2; rules[1].schema = 1;
        rules[1].policy = AV_STRICT; rules[1].payload_limit = 8;
        CHECK(av_open(&context, &config) == AV_INVALID);
    }

    /* Separate NVS namespaces and separate partitions are independent slots. */
    CHECK(av_nvs_open(&b, "avtest", "other", NVS_READWRITE) == ESP_OK);
    CHECK(av_nvs_backend_validate_id(&router, 1));
    CHECK(av_nvs_backend_validate_id(&router, 2));
    configure(&context, &router, routes, &backend, rules, work);
    CHECK(av_nvs_open(&b, "otherpart", "same", NVS_READWRITE) == ESP_OK);
    CHECK(av_nvs_backend_validate_id(&router, 1));
    CHECK(av_nvs_backend_validate_id(&router, 2));
    configure(&context, &router, routes, &backend, rules, work);

    routes[1].store = &a; /* same handle routed under two IDs */
    CHECK(!av_nvs_backend_validate_id(&router, 1));
    CHECK(av_nvs_backend_bind(&router, &backend) == ESP_ERR_INVALID_ARG);
    CHECK(av_nvs_backend_replace(&router, 1, 0, (const uint8_t *)"x", 1) ==
          ESP_ERR_INVALID_ARG);
    routes[1].store = &b;
    routes[1].object_id = routes[0].object_id; /* duplicate route ID */
    CHECK(!av_nvs_backend_validate_id(&router, 1));
    CHECK(av_nvs_backend_bind(&router, &backend) == ESP_ERR_INVALID_ARG);

    CHECK(av_nvs_open(&a, NULL, long_namespace, NVS_READWRITE) == ESP_ERR_INVALID_ARG);
    CHECK(a.last_error == ESP_ERR_INVALID_ARG);
    return 0;
}
