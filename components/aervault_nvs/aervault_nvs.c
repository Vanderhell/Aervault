#include "aervault_nvs.h"
#include <string.h>

static int set_error(av_nvs *s, const char *op, esp_err_t err)
{
    s->last_operation = op;
    s->last_error = err;
    return (int)err;
}

static int copy_label(char *out, size_t capacity, const char *label)
{
    size_t length;
    if (!label) return 0;
    length = strlen(label);
    if (length >= capacity) return 0;
    memcpy(out, label, length + 1);
    return 1;
}

static int read_slot(void *ctx, unsigned slot, uint8_t *out, size_t capacity,
                     size_t *length)
{
    av_nvs *s = (av_nvs *)ctx;
    esp_err_t err;
    if (!s || !s->opened || slot > 1 || !length)
        return s ? set_error(s, "arguments", ESP_ERR_INVALID_ARG) : ESP_ERR_INVALID_ARG;
    *length = 0;
    err = nvs_get_blob(s->handle, s->keys[slot], NULL, length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s->last_operation = "nvs_get_blob(size)";
        s->last_error = err;
        return 1;
    }
    if (err != ESP_OK) return set_error(s, "nvs_get_blob(size)", err);
    if (*length > capacity) return set_error(s, "blob_capacity", ESP_ERR_INVALID_SIZE);
    err = nvs_get_blob(s->handle, s->keys[slot], out, length);
    if (err != ESP_OK) return set_error(s, "nvs_get_blob(read)", err);
    s->last_operation = "nvs_get_blob";
    s->last_error = ESP_OK;
    return 0;
}

static int replace_slot(void *ctx, unsigned slot, const uint8_t *data, size_t size)
{
    av_nvs *s = (av_nvs *)ctx;
    esp_err_t err;
    if (!s || !s->opened || slot > 1 || (!data && size))
        return s ? set_error(s, "arguments", ESP_ERR_INVALID_ARG) : ESP_ERR_INVALID_ARG;
    err = nvs_set_blob(s->handle, s->keys[slot], data, size);
    if (err != ESP_OK) return set_error(s, "nvs_set_blob", err);
    err = nvs_commit(s->handle);
    if (err != ESP_OK) return set_error(s, "nvs_commit", err);
    s->last_operation = "nvs_commit";
    s->last_error = ESP_OK;
    return 0;
}

esp_err_t av_nvs_open(av_nvs *s, const char *partition,
                      const char *name_space, nvs_open_mode_t mode)
{
    esp_err_t err;
    const char *physical_partition = partition ? partition : "nvs";
    if (!s) return ESP_ERR_INVALID_ARG;
    memset(s, 0, sizeof *s);
    if (!copy_label(s->partition, sizeof s->partition, physical_partition) ||
        !copy_label(s->name_space, sizeof s->name_space, name_space))
        return set_error(s, "arguments", ESP_ERR_INVALID_ARG);
    s->keys[0][0] = 's'; s->keys[0][1] = '0';
    s->keys[1][0] = 's'; s->keys[1][1] = '1';
    if (partition)
        err = nvs_open_from_partition(partition, name_space, mode, &s->handle);
    else
        err = nvs_open(name_space, mode, &s->handle);
    s->last_error = err;
    s->last_operation = partition ? "nvs_open_from_partition" : "nvs_open";
    if (err == ESP_OK) s->opened = 1;
    return err;
}

void av_nvs_close(av_nvs *s)
{
    if (s && s->opened) {
        nvs_close(s->handle);
        s->opened = 0;
    }
}

void av_nvs_bind(av_nvs *s, av_object *object)
{
    if (!object) return;
    object->ctx = s;
    object->read = read_slot;
    object->replace = replace_slot;
}

static av_nvs *route_store(av_nvs_router *router, uint32_t object_id)
{
    size_t i; av_nvs *found = NULL; int matched = 0;
    if (!router || !router->routes) return NULL;
    for (i = 0; i < router->count; ++i) {
        if (router->routes[i].object_id == object_id) {
            if (matched) return NULL;
            matched = 1;
            found = router->routes[i].store;
        }
    }
    return found;
}

int av_nvs_backend_read(void *ctx, uint32_t object_id, unsigned slot,
                        uint8_t *out, size_t capacity, size_t *length)
{
    av_nvs *store = route_store((av_nvs_router *)ctx, object_id);
    if (!av_nvs_backend_validate_id(ctx, object_id)) return ESP_ERR_INVALID_ARG;
    return store ? read_slot(store, slot, out, capacity, length) : ESP_ERR_INVALID_ARG;
}

int av_nvs_backend_replace(void *ctx, uint32_t object_id, unsigned slot,
                           const uint8_t *data, size_t length)
{
    av_nvs *store = route_store((av_nvs_router *)ctx, object_id);
    if (!av_nvs_backend_validate_id(ctx, object_id)) return ESP_ERR_INVALID_ARG;
    return store ? replace_slot(store, slot, data, length) : ESP_ERR_INVALID_ARG;
}

int av_nvs_backend_validate_id(void *ctx, uint32_t object_id)
{
    av_nvs_router *router = (av_nvs_router *)ctx;
    av_nvs *store = route_store(router, object_id);
    size_t i;
    if (!store || !store->opened) return 0;
    for (i = 0; i < router->count; ++i) {
        av_nvs *other = router->routes[i].store;
        if (router->routes[i].object_id == object_id || !other) continue;
        if (other == store ||
            (other->opened && !strcmp(other->partition, store->partition) &&
             !strcmp(other->name_space, store->name_space))) return 0;
    }
    return 1;
}

esp_err_t av_nvs_backend_bind(av_nvs_router *router, av_backend *backend)
{
    size_t i, j;
    if (!router || !router->routes || !router->count || !backend)
        return ESP_ERR_INVALID_ARG;
    for (i = 0; i < router->count; ++i) {
        av_nvs *store = router->routes[i].store;
        if (!store || !store->opened) return ESP_ERR_INVALID_ARG;
        for (j = 0; j < i; ++j) {
            av_nvs *prior = router->routes[j].store;
            if (router->routes[i].object_id == router->routes[j].object_id ||
                prior == store ||
                (prior && prior->opened &&
                 !strcmp(prior->partition, store->partition) &&
                 !strcmp(prior->name_space, store->name_space)))
                return ESP_ERR_INVALID_ARG;
        }
    }
    backend->ctx = router;
    backend->read = av_nvs_backend_read;
    backend->replace = av_nvs_backend_replace;
    backend->validate_id = av_nvs_backend_validate_id;
    return ESP_OK;
}
