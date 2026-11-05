#include "nvs.h"
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{ (void)name; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *part, const char *name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle)
{ (void)part; (void)name; (void)mode; *handle = 2; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *d, size_t *n)
{ (void)h; (void)k; (void)d; (void)n; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *d, size_t n)
{ (void)h; (void)k; (void)d; (void)n; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{ (void)h; (void)k; return ESP_OK; }
