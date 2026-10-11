#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>
#include <esp_log.h>

#include "WiFi.hpp"

// Maximum number of stations allowed to be connected in AP mode
#define WIFI_AP_MAX_STA_CONNECTIONS 8

// Gets the size, in bytes, of a member of a struct
#define SIZEOFMEMBER(type, member) sizeof(((type*)0)->member)

// Singleton instance of WiFi class, available everywhere
CWiFi WiFi;

// callback for all events from the network driver
void CWiFi::WiFiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Wi-Fi station initialized
        ESP_LOGI(TAG, "Wi-Fi station connection starting...");

        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        // Wi-Fi station connected, DHCP starting
        ESP_LOGI(TAG, "Wi-Fi station connected, obtaining IP address...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Got an IP address
        const ip_event_got_ip_t* gotIpEvent = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Wi-Fi station got IP address. Address=" IPSTR ", NetMask=" IPSTR ", Gateway=" IPSTR,
            IP2STR(&gotIpEvent->ip_info.ip), IP2STR(&gotIpEvent->ip_info.netmask), IP2STR(&gotIpEvent->ip_info.gw));

        // Notify subscribers of network availability
        WiFi.WiFiStationConnectedEvent.Publish(&gotIpEvent->ip_info);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Wi-Fi station disconnected
        ESP_LOGI(TAG, "Wi-Fi station disconnected; reconnecting...");

        // Notify subscribers of network unavailability
        WiFi.WiFiStationDisconnectedEvent.Publish(nullptr);

        // Start reconnection
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        // Wi-Fi access point initialized
        esp_netif_ip_info_t ipInfo;
        if (esp_netif_get_ip_info(WiFi._netifAP, &ipInfo) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi access point initialized. Address=" IPSTR ", NetMask=" IPSTR ", Gateway=" IPSTR,
                IP2STR(&ipInfo.ip), IP2STR(&ipInfo.netmask), IP2STR(&ipInfo.gw));

            // notify subscribers of access point availability
            WiFi.WiFiAccessPointActiveEvent.Publish(&ipInfo);
        }
        else
            ESP_LOGW(TAG, "Wi-Fi access point initialized. Could not retrieve IP information.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        // A station has connected to our access point
        const wifi_event_ap_staconnected_t* apStaConnEvent = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "Wi-Fi access point station connected. MAC=" MACSTR, MAC2STR(apStaConnEvent->mac));

        // Notify subscribers of station connection
        WiFi.WiFiAccessPointStationConnectedEvent.Publish(&apStaConnEvent->mac);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        // A station has disconnected from our access point
        const wifi_event_ap_stadisconnected_t* apStaDisEvent = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "Wi-Fi access point station disconnected. MAC=" MACSTR, MAC2STR(apStaDisEvent->mac));

        // Notify subscribers of station disconnection
        WiFi.WiFiAccessPointStationDisconnectedEvent.Publish(&apStaDisEvent->mac);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        // Wi-Fi access point stopped
        WiFi.WiFiAccessPointDeactivatedEvent.Publish(nullptr);
    }
}

esp_err_t CWiFi::Initialize() {
    esp_err_t err;

    if (_wifiInitialized)
        return ESP_OK; // Already initialized

    ESP_LOGI(TAG, "Initializing Wi-Fi...");

    // Initialize NVS for MAC address storage
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing flash: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize Wi-Fi driver
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing network: %s", esp_err_to_name(err));
        return err;
    }
    
    // Start networking task to drive events
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing network event loop: %s", esp_err_to_name(err));
        return err;
    }

    // Register Wi-Fi/DHCP events
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler, nullptr, &_eventAnyId);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler, nullptr, &_eventGotIp);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi initialized successfully");
    _wifiInitialized = true;

    return ESP_OK;
}

esp_err_t CWiFi::StartAP(const char* ssid, const char* pass, uint8_t channel) {
    esp_err_t err;

    // Sanity checks
    if (strlen(ssid) > SIZEOFMEMBER(wifi_config_t, ap.ssid))
        return ESP_ERR_INVALID_SIZE;
    if (strlen(pass) + 1 > SIZEOFMEMBER(wifi_config_t, ap.password)) // +1 for null terminator
        return ESP_ERR_INVALID_SIZE;

    ESP_LOGI(TAG, "Starting Wi-Fi access point...");

    // Initialize Wi-Fi access point
    _netifAP = esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifiInitConfig);
    if (err != ESP_OK) return err;

    // Create Wi-Fi access point settings
    wifi_config_t wifiConfig = {};
    memcpy(wifiConfig.ap.ssid, ssid, strlen(ssid));
    wifiConfig.ap.ssid_len = strlen(ssid);
    memcpy(wifiConfig.ap.password, pass, strlen(pass) + 1); // +1 for null terminator
    wifiConfig.ap.channel = channel;
    wifiConfig.ap.max_connection = WIFI_AP_MAX_STA_CONNECTIONS;
    wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;

    // Start Wi-Fi
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &wifiConfig);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi access point started successfully");

    return ESP_OK;
}

void CWiFi::StopAP() {
    ESP_LOGI(TAG, "Stopping Wi-Fi access point...");
    esp_wifi_stop();
}

esp_err_t CWiFi::ConnectSTA(const char* ssid, const char* pass) {
    esp_err_t err;

    // Sanity checks
    if (strlen(ssid) > SIZEOFMEMBER(wifi_config_t, sta.ssid))
        return ESP_ERR_INVALID_SIZE;
    if (strlen(pass) > SIZEOFMEMBER(wifi_config_t, sta.password))
        return ESP_ERR_INVALID_SIZE;

    ESP_LOGI(TAG, "Starting Wi-Fi station...");

    // Initialize Wi-Fi station
    _netifSTA = esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifiInitConfig);
    if (err != ESP_OK) return err;

    // Create Wi-Fi station settings
    wifi_config_t wifiConfig = {};
    memcpy(wifiConfig.sta.ssid, ssid, strlen(ssid));
    memcpy(wifiConfig.sta.password, pass, strlen(pass));
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Start Wi-Fi
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi station started successfully");

    return ESP_OK;
}

void CWiFi::DisconnectSTA() {
    ESP_LOGI(TAG, "Stopping Wi-Fi station...");
    esp_wifi_stop();
}
