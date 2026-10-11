#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_http_server.h>

#include "Network/WiFi.hpp"
#include "Network/WebServer.hpp"

#define AP_SSID    "TestNetwork"
#define AP_KEY     "password"
#define AP_CHANNEL 1

static const char* TAG = "Main";

// API endpoint handler for "/hello"
static esp_err_t ApiEndpointHello(httpd_req_t *req, const char* path, const char* query, const char* fragment)
{
    esp_err_t result;
    const char *resp_str = "Hello from ESP32 HTTP Server!";

    // Set MIME type
    result = httpd_resp_set_type(req, "text/plain");
    if (result != ESP_OK) return result;

    // Send response
    return httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
}

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

    // Register API endpoint
    err = WebServer.RegisterAPIEndpoint("/hello", HTTP_GET, ApiEndpointHello);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register \"hello\" endpoint: %s", esp_err_to_name(err));
        abort();
    }

    // Safe to exit here, as tasks are running
}
