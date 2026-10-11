#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <http_parser.h>
#include <esp_spiffs.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>

#include "WebServer.hpp"

#define BASE_PATH "/httproot" // Internal SPIFFS mount path
#define INDEX_FILE "index.html"
#define SEND_BUFFER_SIZE 256

// Singleton instance of CWebServer class, available everywhere
CWebServer WebServer;

// Struct for holding a MIME type definition
typedef struct {
    const char* Extension;
    const char* MIMEType;
} MimeType_t;

// Handle for the HTTPD server
static httpd_handle_t __httpdHandle;

// Array of registered MIME types for static files
static MimeType_t __mimeTypes[] = {
    { ".html", "text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".png",  "image/png" },
    { ".ico",  "image/x-icon" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "text/css" },
    { ".txt",  "text/plain" },
};

// Determines if the provided string ends with another string
static bool EndsWith(const char* str, const char* suffix) {
    if (!str || !suffix)
        return false;

    // Get parameter string lengths
    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);

    if (suffixLen > strLen)
        return false; // Can't end in suffix if suffix is longer than str

    // Move forward in str so suffixLen chars remain, and do the comparison
    return strcmp(str + (strLen - suffixLen), suffix) == 0;
}

// Given a file name, sets the HTTP request's Content-Type header to the correct MIME type, if found
static esp_err_t SetContentTypeFromFileName(httpd_req_t* req, const char* fileName) {
    // Search for a matching registered MIME type
    for (uint8_t i = 0; i < sizeof(__mimeTypes) / sizeof(__mimeTypes[0]); i++)
        if (EndsWith(fileName, __mimeTypes[i].Extension))
            return httpd_resp_set_type(req, __mimeTypes[i].MIMEType);

    // Default MIME type
    return httpd_resp_set_type(req, "application/octet-stream");
}

// Splits a URI into path, querry and fragment strings (path?query#fragment)
// Will return nullptr for query if not found
// Caller must free() these strings if function returns success
static bool SplitRequestUri(const char* uri, char** path, char** query, char** fragment) {
    // Initial output values
    *path = nullptr;
    *query = nullptr;
    *fragment = nullptr;

    // Parse the URI
    struct http_parser_url httpUrl;
    int isConnectReq = 0;
    int result = http_parser_parse_url(uri, strlen(uri), isConnectReq, &httpUrl);
    if (result != 0) {
        // Failed
        return false;
    }

    // Extract path
    if (httpUrl.field_set & (1 << UF_PATH)) {
        // Path found
        *path = strndup(uri + httpUrl.field_data[UF_PATH].off, httpUrl.field_data[UF_PATH].len);
        if (!*path)
            // Failed
            return false;
    }

    // Extract query
    if (httpUrl.field_set & (1 << UF_QUERY)) {
        // Query found
        *query = strndup(uri + httpUrl.field_data[UF_QUERY].off, httpUrl.field_data[UF_QUERY].len);
        if (!*query) {
            // Failed
            if (*path)
                free(*path);
            return false;
        }
    }

    // Extract fragment
    if (httpUrl.field_set & (1 << UF_FRAGMENT)) {
        // Fragment found
        *fragment = strndup(uri + httpUrl.field_data[UF_FRAGMENT].off, httpUrl.field_data[UF_FRAGMENT].len);
        if (!*fragment) {
            // Failed
            if (*path)
                free(*path);
            if (*query)
                free(*query);
            return false;
        }
    }
    
    // Success
    return true;
}

// Normalizes the requested URI path and appends index file as needed
// Returns nullptr if failed
// Caller must free() result if call succeeds
static char* GetRequestedFile(const char* path) {
    char* fullPath;
    char* file;

    // Determine the file name to send
    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        // Requesting index on root
        if (asprintf(&fullPath, "/%s", INDEX_FILE) < 0)
            // Out of memory
            return nullptr;
    } else if (EndsWith(path, "/")) {
        // Requesting index on a sub-path
        if (asprintf(&fullPath, "%s/%s", path, INDEX_FILE) < 0)
            // Out of memory
            return nullptr;
    } else {
        // Direct file request
        fullPath = strdup(path);
        if (!fullPath)
            // Out of memory
            return nullptr;
    }

    // Collapse path traversals into canonical path
    file = realpath(fullPath, nullptr);

    // Clean up intermediate path string
    free(fullPath);

    // Return result (will be nullptr if out of memory)
    return file;
}

// Prepends root path to file, returning "root/file"
// Returns nullptr if out of memory
// Caller must free() result
static char* PrependPath(const char* root, const char* file) {
    char* fullPath;

    if (*file == '/') {
        if (asprintf(&fullPath, "%s%s", root, file) < 0)
            // Out of memory
            return nullptr;
    } else
        if (asprintf(&fullPath, "%s/%s", root, file) < 0)
            // Out of memory
            return nullptr;

    return fullPath;
}

// Streams the given file from the file system
static esp_err_t SendFile(httpd_req_t*req, const char* file) {
    esp_err_t result = ESP_OK;
    FILE* fd = nullptr;
    char* sendBuff = nullptr;
    size_t bytesRead = 0;

    // Determine if file exists
    struct stat fileStat;
    if (stat(file, &fileStat) == -1) {
        // File not found, so send a 404 error
        ESP_LOGW(CWebServer::TAG, "File \"%s\" not found", file);
        result = httpd_resp_send_404(req);
        if (result != ESP_OK)
            // Failed to send error
            ESP_LOGW(CWebServer::TAG, "Failed to send HTTP 404 for file \"%s\": %s", file, esp_err_to_name(result));
        return result;
    }

    // Open the file for reading
    fd = fopen(file, "r");
    if (!fd) {
        ESP_LOGE(CWebServer::TAG, "Failed to open file \"%s\" for reading", file);
        result = httpd_resp_send_500(req);
        if (result != ESP_OK)
            // Failed to send error
            ESP_LOGW(CWebServer::TAG, "Failed to send HTTP 500 for file \"%s\": %s", file, strerror(errno));
        return result;
    }

    sendBuff = (char*)malloc(SEND_BUFFER_SIZE);
    if (!sendBuff) {
        // Out of memory
        if (fd)
            fclose(fd);

        return ESP_ERR_NO_MEM;
    }

    // send the file
    do {
        bytesRead = fread(sendBuff, 1, SEND_BUFFER_SIZE, fd);

        if (bytesRead > 0) {
            result = httpd_resp_send_chunk(req, sendBuff, bytesRead);
            if (result != ESP_OK) {
                // Send failure
                // Abort the send cleanly
                httpd_resp_send_chunk(req, nullptr, 0);
                ESP_LOGW(CWebServer::TAG, "Failed to send chunk for \"%s\": %s", file, esp_err_to_name(result));

                // Clean up send buffer
                free(sendBuff);

                // Close file
                if (fd)
                    fclose(fd);

                return result;
            }
        }
    } while (bytesRead > 0);

    // Complete the send
    result = httpd_resp_send_chunk(req, nullptr, 0);

    // Clean up send buffer
    free(sendBuff);

    // Close file
    if (fd)
        fclose(fd);

    return result;
}

esp_err_t CWebServer::Initialize() {
    esp_err_t result;

    ESP_LOGI(TAG, "Initializing web server...");

    // Mount SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = BASE_PATH,        // Base path where files will be accessed (e.g. /spiffs/index.html)
        .partition_label = NULL,       // Automatically picks the first partition marked 'spiffs'
        .max_files = 5,                // Maximum number of files open simultaneously
        .format_if_mount_failed = true // Automatically formats the partition if it is empty/corrupted
    };
    result = esp_vfs_spiffs_register(&conf);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize file system: %s", esp_err_to_name(result));
        return result;
    }

    // Verify storage space allocation
    size_t total = 0, used = 0;
    result = esp_spiffs_info(NULL, &total, &used);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify file system: %s", esp_err_to_name(result));
        return result;
    }
    //ESP_LOGI(TAG, "File system: Size=%d, Used=%d", total, used);

    ESP_LOGI(TAG, "Web server initialized successfully");
    ESP_LOGI(TAG, "Starting web server...");

    // Set up web server config
    httpd_config_t httpdConfig = HTTPD_DEFAULT_CONFIG();
    httpdConfig.uri_match_fn = httpd_uri_match_wildcard;
    httpdConfig.stack_size = 8192; // More space for JSON
    httpdConfig.lru_purge_enable = true;
    httpdConfig.max_open_sockets = 7; 

    // Start web server
    result = httpd_start(&__httpdHandle, &httpdConfig);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(result));
        return result;
    }

    // Register request hanlder
    httpd_uri_t handlerConfig = {};
    handlerConfig.uri = "*";
    handlerConfig.method = (httpd_method_t)HTTP_ANY;
    handlerConfig.handler = CWebServer::HttpRequestHandler;
    handlerConfig.user_ctx = this; // Use this as context, so handler has access to this class instance
    result = httpd_register_uri_handler(__httpdHandle, &handlerConfig);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register web server request handler: %s", esp_err_to_name(result));
        httpd_stop(__httpdHandle);
        return result;
    }

    ESP_LOGI(TAG, "Web server started successfully");

    return ESP_OK;
}

esp_err_t CWebServer::RegisterAPIEndpoint(const char* path, http_method method, WebServerApiHandler_t handler) {
    // Find next free API handler slot
    int slot = -1;
    for (int i = 0; i < MAX_WEB_SERVER_API_HANDLERS; i++)
        if (!__webApiHandlerRegistrations[i].Handler) {
            slot = i;
            break;
        }
    if (slot < 0) {
        // No free handler slots
        ESP_LOGW(TAG, "Attempted to register API handler, but no free slots left");
        return ESP_ERR_NO_MEM;
    }

    // Create the new registration
    __webApiHandlerRegistrations[slot].Path = strdup(path);
    if (!__webApiHandlerRegistrations[slot].Path)
        // Out of memory
        return ESP_ERR_NO_MEM;
    __webApiHandlerRegistrations[slot].Method = method;
    __webApiHandlerRegistrations[slot].Handler = handler;

    return ESP_OK;
}

esp_err_t CWebServer::UnregisterAPIEndpoint(const char* path, http_method method) {
    // Try to find the refrenced API handler, and discard it
    for (int i = 0; i < MAX_WEB_SERVER_API_HANDLERS; i++)
        if (__webApiHandlerRegistrations[i].Handler &&
            method == __webApiHandlerRegistrations[i].Method &&
            strcmp(__webApiHandlerRegistrations[i].Path, path) == 0)
        {
            // Handler found
            __webApiHandlerRegistrations[i] = {};
            return ESP_OK;
        }

    ESP_LOGW(TAG, "Attempted to unregister API handler, but entry not found");

    return ESP_ERR_NOT_FOUND;
}

esp_err_t CWebServer::HttpRequestHandler(httpd_req_t* req) {
    CWebServer* server = (CWebServer*)req->user_ctx;
    esp_err_t result = ESP_OK;
    char* path = nullptr;
    char* query = nullptr;
    char* fragment = nullptr;
    char *file = nullptr;
    char *fullFilePath = nullptr;

    ESP_LOGD(TAG, "Http request: Method=%d, URI=%s", req->method, req->uri);

    // Extract URI elements
    if (!SplitRequestUri(req->uri, &path, &query, &fragment))
        // Out of memory
        goto outOfMemory;

    // path, quey and fragment must be free()'d before exiting this function

    // Try to find an API handler for this call
    for (int i = 0; i < MAX_WEB_SERVER_API_HANDLERS; i++)
        if (server->__webApiHandlerRegistrations[i].Handler &&
        req->method == server->__webApiHandlerRegistrations[i].Method &&
        strcmp(server->__webApiHandlerRegistrations[i].Path, path) == 0)
            // Handler found
            return server->__webApiHandlerRegistrations[i].Handler(req, path, query, fragment);

    // Fall through to static file handler
    ESP_LOGD(TAG, "Serving static file");
    if (path)     ESP_LOGD(TAG, "    path=%s",     path);
    if (query)    ESP_LOGD(TAG, "    query=%s",    query);
    if (fragment) ESP_LOGD(TAG, "    fragment=%s", fragment);

    // Determine the file name to send
    file = GetRequestedFile(path);
    if (!file)
        // Out of memory
        goto outOfMemory;

    ESP_LOGD(TAG, "    file=%s", file);

    // Prepend base path, after path traversal was taken care of
    // Effectively, a chroot() into BASE_PATH
    fullFilePath = PrependPath(BASE_PATH, file);
    if (!fullFilePath)
        // Out of memory
        goto outOfMemory;

    ESP_LOGD(TAG, "    fullFilePath=%s", fullFilePath);

    // Set the Content-Type header
    SetContentTypeFromFileName(req, file);

    // Send the file
    result = SendFile(req, fullFilePath);

done:
    // Free any allocated buffers
    if (fullFilePath)
        free(fullFilePath);
    if (file)
        free(file);
    if (fragment)
        free(fragment);
    if (query)
        free(query);
    if (path)
        free(path);

    return result;

outOfMemory:
    ESP_LOGE(TAG, "Out of memory");
    // Send error to caller
    result = httpd_resp_send_500(req);
    if (result != ESP_OK)
        // Failed to send error
        ESP_LOGW(TAG, "Failed to send HTTP 500: %s", strerror(errno));
    else
        result = ESP_ERR_NO_MEM;

    goto done;
}
