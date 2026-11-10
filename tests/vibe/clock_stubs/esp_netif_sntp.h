#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
typedef struct {
    bool start,wait_for_sync,renew_servers_after_new_IP;
    void (*sync_cb)(void *);
    const char *servers[1];
} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) {.start=true,.wait_for_sync=true,.servers={server}}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_reachability(unsigned index,unsigned *reachability);
void esp_netif_sntp_deinit(void);
