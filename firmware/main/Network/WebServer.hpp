#pragma once

#include <inttypes.h>
#include <esp_err.h>
#include <esp_http_server.h>

// Maximum number of registered API endpoint handlers
#define MAX_WEB_SERVER_API_HANDLERS 32

typedef esp_err_t (*WebServerApiHandler_t)(httpd_req_t* req, const char* path, const char* query, const char* fragment);

typedef struct {
    const char* Path;
    http_method Method;
    WebServerApiHandler_t Handler;
} WebServerApiHandlerRegistration_t;

class CWebServer {
    public:
        static constexpr const char* TAG = "CWebServer";

        /// @brief Initializes the web server.
        /// @return Any error, or ESP_OK.
        esp_err_t Initialize();

        /// @brief Registers an HTTP API endpoint handler.
        /// @param uri The URI to host this endpoint on. Must start with a forward slash. Caller may free() string once registered.
        /// @param method The HTTP method to respond to. Values: HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE.
        /// @param handler The callback to run to handle the request.
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t RegisterAPIEndpoint(const char* path, http_method method, WebServerApiHandler_t handler);

        /// @brief Unregisters an HTTP API endpoint handler.
        /// @param uri The URI this endpoint is hosted on. Must start with a forward slash.
        /// @param method The HTTP method the handler responds to. Values: HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE.
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t UnregisterAPIEndpoint(const char* path, http_method method);

    private:
        // Storage for web API handler definitions
        WebServerApiHandlerRegistration_t __webApiHandlerRegistrations[MAX_WEB_SERVER_API_HANDLERS] = {};

        // Handler for static files, loads from SPIFFS
        static esp_err_t HttpRequestHandler(httpd_req_t *req);
    
};

// Make the static WebServer instance globally available
extern CWebServer WebServer;
