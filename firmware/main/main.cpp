#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>

#include "Network/WiFi.hpp"
#include "Network/WebServer.hpp"
#include "Network/Api.hpp"
#include "Hardware/LightDriver.hpp"

#define AP_SSID           "TestNetwork"
#define AP_KEY            "password"
#define AP_CHANNEL        1

#define LIGHTS_GPIO_DATA  5
#define LIGHTS_GPIO_CLK   6
#define LIGHTS_GPIO_LOAD  7
#define LIGHTS_NUM_MOD    4

static const char* TAG = "Main";

extern "C" void app_main(void)
{
    esp_err_t err;

    // Set log levels
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set(CWiFi::TAG, ESP_LOG_INFO);
    esp_log_level_set(CWebServer::TAG, ESP_LOG_INFO);

    // Initialize networking
    err = WiFi.Initialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
        abort();
    }

    // Start an access point
    err = WiFi.StartAP(AP_SSID, AP_KEY, AP_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi access point: %s", esp_err_to_name(err));
        abort();
    }

    // Initialize web server
    // This needs to happen after WiFi.Initialize(), but does not need to happen after WiFi.StartAP() or WiFi.ConnectSTA()
    err = WebServer.Initialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize web server: %s", esp_err_to_name(err));
        abort();
    }

    // initialize API
    err = Api.Initialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize API: %s", esp_err_to_name(err));
        abort();
    }

    // Initialize light driver
    err = LightDriver.Initialize(LIGHTS_GPIO_DATA, LIGHTS_GPIO_CLK, LIGHTS_GPIO_LOAD, LIGHTS_NUM_MOD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize light driver: %s", esp_err_to_name(err));
        abort();
    }

    // Start light driver with all outputs off
    err = LightDriver.Clear();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear light driver state: %s", esp_err_to_name(err));
        abort();
    }

    // Safe to exit here, as tasks are running
}
