#ifndef AERVAULT_NVS_H
#define AERVAULT_NVS_H
#include "aervault.h"
#include "nvs.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* One namespace is exclusively owned by one AV object (two slots). */
typedef struct {
    nvs_handle_t handle;
    esp_err_t last_error;
    const char *last_operation;
    char keys[2][16];
    char partition[17];
    char name_space[16];
    int opened;
} av_nvs;

typedef struct {
    uint32_t object_id;
    av_nvs *store;
} av_nvs_route;

typedef struct {
    const av_nvs_route *routes;
    size_t count;
} av_nvs_router;

esp_err_t av_nvs_open(av_nvs *store, const char *partition,
                      const char *name_space, nvs_open_mode_t mode);
void av_nvs_close(av_nvs *store);
void av_nvs_bind(av_nvs *store, av_object *object);
/* ID-based callbacks for av_config. Routes are caller-owned and immutable. */
int av_nvs_backend_read(void *, uint32_t, unsigned, uint8_t *, size_t, size_t *);
int av_nvs_backend_replace(void *, uint32_t, unsigned, const uint8_t *, size_t);
int av_nvs_backend_validate_id(void *, uint32_t);
/* Validates route IDs and physical (partition, namespace) slot uniqueness,
 * then fills every callback including av_backend.validate_id. */
esp_err_t av_nvs_backend_bind(av_nvs_router *, av_backend *);

#endif
