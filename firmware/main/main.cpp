#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>

#include "DataTypes.hpp"
#include "Storage/Storage.hpp"
#include "Storage/AnimationStore.hpp"
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
#define LIGHTS_NUM_MOD    11

static const char* TAG = "Main";

extern "C" void app_main(void)
{
    Result_t result;

    // Set log levels
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set(CStorage::TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CAnimationStore::TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CWiFi::TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CWebServer::TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CApi::TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CLightDriver::TAG, ESP_LOG_DEBUG);

    // Initialize animation storage
    result = AnimationStore.Initialize();
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize animation storage: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Initialize networking
    result = WiFi.Initialize();
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Start an access point
    result = WiFi.StartAP(AP_SSID, AP_KEY, AP_CHANNEL);
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to start Wi-Fi access point: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Initialize web server
    // This needs to happen after WiFi.Initialize(), but does not need to happen after WiFi.StartAP() or WiFi.ConnectSTA()
    result = WebServer.Initialize();
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize web server: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // initialize API
    result = Api.Initialize();
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize API: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Initialize light driver
    result = LightDriver.Initialize(LIGHTS_GPIO_DATA, LIGHTS_GPIO_CLK, LIGHTS_GPIO_LOAD, LIGHTS_NUM_MOD);
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize light driver: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Start light driver with all outputs off
    result = LightDriver.Clear();
    if (result != Result_t::OK) {
        const std::string_view resultStr = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to clear light driver state: %.*s", resultStr.size(), resultStr.data());
        abort();
    }

    // Safe to exit here, as tasks are running

    uint8_t temp;
    LightState_t state {};
    for (;;) {

        for (uint8_t bit = 0; bit < 7; bit++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            temp = 1 << bit;
            for (uint8_t i = 0; i < 11; i++)
                state.at(i) = temp;
            LightDriver.Update(state);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        temp = 0xFF;
        for (uint8_t i = 0; i < 11; i++)
            state.at(i) = temp;
        LightDriver.Update(state);

        vTaskDelay(pdMS_TO_TICKS(1000));
        temp = 0x00;
        for (uint8_t i = 0; i < 11; i++)
            state.at(i) = temp;
        LightDriver.Update(state);
    }
}
