#include <array>
#include <new>
#include <fstream>
#include <memory>
#include <string>
#include <cstring>
#include <string_view>
#include <filesystem>
#include <system_error>

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
#include <cJSON.h>

#include "../Storage/Storage.hpp"
#include "../DataTypes.hpp"

#include "WebServer.hpp"

#define STORAGE_LABEL "www" // SPIFFS partition label
#define MOUNT_POINT "/www" // Internal SPIFFS mount path
#define INDEX_FILE "index.html"
#define SEND_BUFFER_SIZE 1024

#define MAX_REQUEST_BODY_LEN 32768
#define MAX_QUERY_STRING_LEN 2048

// Singleton instance of CWebServer class, available everywhere
CWebServer WebServer;

// Struct for holding a MIME type definition
struct MimeType_t {
    const std::string_view Extension;
    const std::string_view MIMEType;
};

// Struct for holding the return values from splitRequestUri()
struct UriParts_t {
    std::string path;
    std::optional<std::string> query;
    std::optional<std::string> fragment;
};

// UriParts_t RAII type
using UriPartsPtr = std::unique_ptr<UriParts_t>;

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

static std::string_view GetHttpStatusString(HttpStatus_t status) {
    switch (status) {
        case HttpStatus_t::HTTP200OK:                    return "200 OK";
        case HttpStatus_t::HTTP201Created:               return "201 Created";
        case HttpStatus_t::HTTP202Accepted:              return "202 Accepted";
        case HttpStatus_t::HTTP204NoContent:             return "204 No Content";
        case HttpStatus_t::HTTP301MovedPermanently:      return "301 Moved Permanently";
        case HttpStatus_t::HTTP302Found:                 return "302 Found";
        case HttpStatus_t::HTTP304NotModified:           return "304 Not Modified";
        case HttpStatus_t::HTTP400BadRequest:            return "400 Bad Request";
        case HttpStatus_t::HTTP401Unauthorized:          return "401 Unauthorized";
        case HttpStatus_t::HTTP403Forbidden:             return "403 Forbidden";
        case HttpStatus_t::HTTP404NotFound:              return "404 Not Found";
        case HttpStatus_t::HTTP405MethodNotAllowed:      return "405 Method Not Allowed";
        case HttpStatus_t::HTTP406NotAcceptable:         return "406 Not Acceptable";
        case HttpStatus_t::HTTP408RequestTimeout:        return "408 Request Timeout";
        case HttpStatus_t::HTTP413PayloadTooLarge:       return "413 Payload Too Large";
        case HttpStatus_t::HTTP414URITooLong:            return "414 URI Too Long";
        case HttpStatus_t::HTTP415UnsupportedMediaType:  return "415 Unsupported Media Type";
        case HttpStatus_t::HTTP422UnprocessableContent:  return "422 Unprocessable Content";
        case HttpStatus_t::HTTP429TooManyRequests:       return "429 Too Many Requests";
        case HttpStatus_t::HTTP500InternalServerError:   return "500 Internal Server Error";
        case HttpStatus_t::HTTP503ServiceUnavailable:    return "503 Service Unavailable";
        default:                                         return "418 I'm a teapot"; // No idea what to put here
    }
}

// Returns true if the passed char is a hex digit (0-9/a-f/A-F)
static bool IsHexDigit(char digit) {
    return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
}

// Parses the supplied hex digit into a value, 0 - 15
// If digit is invalid, returns 0
static uint8_t ParseHexDigit(char digit) {
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    else
        return 0;
}

// Builds an RFC 9457 Problem Details JSON response from minimal fields
// title is the short problem name
// statusCode is the HTTP status code being returned
// detail is the description of the problem
// instance is the URI path where the problem occurred
static Result<CJsonPtr> BuildProblemDetails(const std::string_view title, uint16_t statusCode,
    const std::string_view detail, const std::string_view instance)
{
    try {
        // Create JSON object
        CJsonPtr response = CJsonPtr(cJSON_CreateObject(), &cJSON_Delete);
        if (!response)
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Add title
        std::string str;
        str = std::string(title);
        if (!cJSON_AddStringToObject(response.get(), "title", str.c_str()))
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Add status
        if (!cJSON_AddNumberToObject(response.get(), "status", statusCode))
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Add detail
        str = std::string(detail);
        if (!cJSON_AddStringToObject(response.get(), "detail", str.c_str()))
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Add instance
        str = std::string(instance);
        if (!cJSON_AddStringToObject(response.get(), "instance", str.c_str()))
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        return Result<CJsonPtr>::Ok(std::move(response));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
    }
}

// Given a file name, sets the HTTP request's Content-Type header to the correct MIME type, if found
static Result_t SetContentTypeFromFileName(httpd_req_t* req, const std::string_view fileName) {
    try {
        // Search for a matching registered MIME type
        for (uint8_t i = 0; i < sizeof(__mimeTypes) / sizeof(__mimeTypes[0]); i++)
            if (fileName.ends_with(__mimeTypes[i].Extension))
                return EspErrToResult(httpd_resp_set_type(req, __mimeTypes[i].MIMEType.data()));
                // It's okay to use std::string_view.data() here, because the view is constructed
                // from a null terminated string anyway

        // Default MIME type
        return EspErrToResult(httpd_resp_set_type(req, "application/octet-stream"));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

// Splits a URI into path, querry and fragment strings (path?query#fragment)
static Result<UriPartsPtr> SplitRequestUri(const std::string_view uri) {
    try {
        struct http_parser_url httpUrl {};
        int isConnectReq = 0;

        // Parse the URI
        std::string uriString(uri);
        if (http_parser_parse_url(uriString.c_str(), uriString.size(), isConnectReq, &httpUrl))
            return Result<UriPartsPtr>::Err(Result_t::Failed);

        // Allocate the struct
        UriPartsPtr parts = std::make_unique<UriParts_t>();

        // Extract path
        if (httpUrl.field_set & (1 << UF_PATH))
            // Path found
            parts->path = std::string(uri.substr(httpUrl.field_data[UF_PATH].off, httpUrl.field_data[UF_PATH].len));

        // Extract query
        if (httpUrl.field_set & (1 << UF_QUERY))
            // Query found
            parts->query = std::string(uri.substr(httpUrl.field_data[UF_QUERY].off, httpUrl.field_data[UF_QUERY].len));

        // Extract fragment
        if (httpUrl.field_set & (1 << UF_FRAGMENT))
            // Fragment found
            parts->fragment = std::string(uri.substr(httpUrl.field_data[UF_FRAGMENT].off, httpUrl.field_data[UF_FRAGMENT].len));
        
        // Success
        return Result<UriPartsPtr>::Ok(std::move(parts));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<UriPartsPtr>::Err(Result_t::OutOfMemory);
    }
}

Result_t CWebServer::Initialize() {
    // Mount file system
    Result_t result = Storage.Mount(FileSystemType_t::SPIFFS, STORAGE_LABEL, MOUNT_POINT, false);
    if (result != Result_t::OK) {
        std::string_view error = GetResultDescription(result);
        ESP_LOGE(TAG, "Failed to initialize web server's file system: %.*s", error.size(), error.data());
        return result;
    }

    ESP_LOGI(TAG, "Starting web server...");

    // Set up web server config
    httpd_config_t httpdConfig = HTTPD_DEFAULT_CONFIG();
    httpdConfig.uri_match_fn = httpd_uri_match_wildcard;
    httpdConfig.stack_size = 8192; // More space for JSON
    httpdConfig.lru_purge_enable = true;
    httpdConfig.max_open_sockets = 7; 

    // Start web server
    esp_err_t err = httpd_start(&__httpdHandle, &httpdConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(err));
        return EspErrToResult(err);
    }

    // Register request hanlder
    httpd_uri_t handlerConfig = {};
    handlerConfig.uri = "*";
    handlerConfig.method = (httpd_method_t)HTTP_ANY;
    handlerConfig.handler = CWebServer::HttpRequestHandler;
    handlerConfig.user_ctx = this; // Use this as context, so handler has access to this class instance
    err = httpd_register_uri_handler(__httpdHandle, &handlerConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register web server request handler: %s", esp_err_to_name(err));
        httpd_stop(__httpdHandle);
        return Result_t::Failed;
    }

    ESP_LOGI(TAG, "Web server started successfully");

    return Result_t::OK;
}

Result_t CWebServer::RegisterAPIEndpoint(const char* path, http_method method, WebServerApiHandler_t handler) {
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
        return Result_t::NoSlotsFree;
    }

    // Create the new registration
    __webApiHandlerRegistrations[slot].Path = strdup(path);
    if (!__webApiHandlerRegistrations[slot].Path)
        // Out of memory
        return Result_t::OutOfMemory;
    __webApiHandlerRegistrations[slot].Method = method;
    __webApiHandlerRegistrations[slot].Handler = handler;

    return Result_t::OK;
}

Result_t CWebServer::UnregisterAPIEndpoint(const char* path, http_method method) {
    // Try to find the refrenced API handler, and discard it
    for (int i = 0; i < MAX_WEB_SERVER_API_HANDLERS; i++)
        if (__webApiHandlerRegistrations[i].Handler &&
            method == __webApiHandlerRegistrations[i].Method &&
            strcmp(__webApiHandlerRegistrations[i].Path, path) == 0)
        {
            // Handler found
            __webApiHandlerRegistrations[i] = {};
            return Result_t::OK;
        }

    ESP_LOGW(TAG, "Attempted to unregister API handler, but entry not found");

    return Result_t::EntryNotFound;
}

// Finds a handler for the given request path
WebServerApiHandlerRegistration_t* CWebServer::FindHandler(httpd_req_t* req, const std::string_view requestPath) {
    WebServerApiHandlerRegistration_t* reg;

    for (int i = 0; i < MAX_WEB_SERVER_API_HANDLERS; i++) {
        reg = &__webApiHandlerRegistrations[i];

        if (reg->Handler && req->method == reg->Method) {
            // Check the handler path
            const char* handlerPathPtr = reg->Path;
            const char* requestPathPtr = requestPath.data();

            for (;; handlerPathPtr++, requestPathPtr++) {
                if (*handlerPathPtr == '\0' && *requestPathPtr == '\0')
                    return reg; // Exact match

                if (*handlerPathPtr == '\0' && *requestPathPtr != '\0')
                    break; // Path length mismatch

                if (*handlerPathPtr != '\0' && *requestPathPtr == '\0')
                    break; // Path length mismatch

                if (*handlerPathPtr == '?')
                    continue; // Single-character wildcard (must match one char)

                if (*handlerPathPtr == '*')
                    return reg; // Multi-character wildcard (must match one char)

                if (*handlerPathPtr != *requestPathPtr)
                    break; // Mismatch
            }
        }
    }
    
    return nullptr;
}

esp_err_t CWebServer::HttpRequestHandler(httpd_req_t* req) {
    CWebServer* server = (CWebServer*)req->user_ctx;

    ESP_LOGD(TAG, "Http request: Method=%d, URI=%s", req->method, req->uri);

    try {
        // Extract URI elements
        const Result<UriPartsPtr> uriParts = SplitRequestUri(req->uri);
        if (!uriParts)
            return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
                GetResultDescription(uriParts.error())) != Result_t::OK;

        // Try processing with an API handler first
        WebServerApiHandlerRegistration_t* reg = server->FindHandler(req, uriParts.value()->path);
        if (reg)
            // Found a handler, so we use it
            return (reg->Handler(req, uriParts.value()->path, uriParts.value()->query, uriParts.value()->fragment)) ? ESP_OK : ESP_FAIL;
        else {
            // No handler, try a static file
            std::string path = std::move(uriParts.value()->path);
            std::optional<std::string> query = std::move(uriParts.value()->query);
            std::optional<std::string> fragment = std::move(uriParts.value()->fragment);

            // Log the access
            ESP_LOGD(TAG, "Serving static file");
            ESP_LOGD(TAG, "    path=%.*s", path.size(), path.data());
            if (uriParts.value()->query)
                ESP_LOGD(TAG, "    query=%.*s", query.value().size(), query.value().data());
            if (uriParts.value()->fragment)
                ESP_LOGD(TAG, "    fragment=%.*s", fragment.value().size(), fragment.value().data());

            // Work out what file the client is requesting
            if (path.empty()) {
                // Not a valid request, but we'll serve root anyway
                path += "/" INDEX_FILE;
            } if (path == "/") {
                // Client requesting index on root
                path += INDEX_FILE;
            } if (path.ends_with('/')) {
                // client requesting index on sub-path
                path += INDEX_FILE;
            }

            // Determine the file name to send
            Result<std::string> file = Storage.GetFilePath(MOUNT_POINT, path);
            if (!file)
                return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
                    GetResultDescription(file.error())) != Result_t::OK;

            ESP_LOGD(TAG, "    fullFilePath=%.*s", file.value().size(), file.value().data());

            // Send the file
            Result_t result = SendHttpResponseFile(req, HttpStatus_t::HTTP200OK, file.value());

            switch (result) {
                case Result_t::OK:
                    // Success
                    return ESP_OK;

                case Result_t::FileNotFound:
                    return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP404NotFound,
                        GetResultDescription(result)) != Result_t::OK;
                
                default:
                    // All other errors
                    return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
                        GetResultDescription(result)) != Result_t::OK;
            }
        }
    } catch (const std::bad_alloc&) {
        // Out of memory
        return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
            GetResultDescription(Result_t::OutOfMemory)) != Result_t::OK;
    } catch (...) {
        // Unknown failure
        return SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
            GetResultDescription(Result_t::Failed)) != Result_t::OK;
    }
}

Result<std::string> CWebServer::UrlDecode(const std::string_view source) {
    try {
        // Allocate string for output
        size_t sourceLength = source.length();
        std::string output;
        output.reserve(sourceLength);

        // Iterate over all chars in the string
        size_t i = 0;
        while (i < sourceLength) {
            if (source[i] == '+') {
                output.push_back(' ');
            } else if (source[i] == '%') {
                // Start of a hex escape code
                // Sanity checks
                if (sourceLength - i < 3 || !IsHexDigit(source[i + 1]) || !IsHexDigit(source[i + 2]))
                    // Not a valid hex escape sequence
                    return Result<std::string>::Err(Result_t::InvalidArg);

                // Parse the hex values
                uint8_t a = ParseHexDigit(source[i + 1]);
                uint8_t b = ParseHexDigit(source[i + 2]);

                // Save the new character
                output.push_back(static_cast<char>((a << 4) | b));

                // Increment the source index
                i += 3;
            } else {
                // No escaping, copy character as-is and increment the source index
                output.push_back(source[i++]);
            }
        }

        // Resize output string if needed, because memory is at a premium
        if (output.size() < output.capacity())
            output.shrink_to_fit();

        // Success
        return Result<std::string>::Ok(std::move(output));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<std::string>::Err(Result_t::OutOfMemory);
    }
}

Result<std::string> CWebServer::GetQueryParam(const std::string_view queryString, const std::string& keyName) {
    // Sanity check
    if (queryString.size() > MAX_QUERY_STRING_LEN)
        return Result<std::string>::Err(Result_t::RequestTooBig);

    try {
        // Create a null-terminated copy of "queryString" so we can pass it to ESP-IDF C APIs
        std::string query = std::string(queryString);

        // Create a buffer for storing the output of httpd_query_key_value
        // The entire query length is an upper bound for any parameter value
        const size_t valueCapacity = query.size() + 1; // Again, +1 for null terminator
        BufferPtr valueBuffer(static_cast<char*>(malloc(valueCapacity)), &std::free);
        if (!valueBuffer)
            // Out of memory
            return Result<std::string>::Err(Result_t::OutOfMemory);

        // Get the query value, if it exists
        const esp_err_t err = httpd_query_key_value(query.c_str(), keyName.c_str(), valueBuffer.get(), valueCapacity);
        if (err == ESP_ERR_HTTPD_RESULT_TRUNC)
            // Param length too small for buffer
            return Result<std::string>::Err(Result_t::BufferTooSmall);
        else if (err != ESP_OK)
            // Value not found
            return Result<std::string>::Err(Result_t::ParamNotFound);

        // Decode the value, set "result", and return the value
        const std::string_view rawValue(valueBuffer.get());
        return UrlDecode(rawValue);
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<std::string>::Err(Result_t::OutOfMemory);
    }
}

Result<CJsonPtr> CWebServer::ReceiveRequestJsonBody(httpd_req_t *req) {
    // Sanity check
    if (!req)
        return Result<CJsonPtr>::Err(Result_t::InvalidArg);
    if (req->content_len == 0)
        return Result<CJsonPtr>::Err(Result_t::RequestBodyEmpty);

    try {
        // Limit request size
        if (req->content_len <= 0 || req->content_len > MAX_REQUEST_BODY_LEN)
            return Result<CJsonPtr>::Err(Result_t::RequestTooBig);

        // Allocate buffer to receive entire POST
        BufferPtr postBuff(static_cast<char*>(malloc(req->content_len + 1)), &std::free); // +1 for null terminator
        if (!postBuff)
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Read the entire POST request
        size_t bytesReceived = 0;
        while (bytesReceived < req->content_len) {
            const int ret = httpd_req_recv(req, postBuff.get() + bytesReceived, req->content_len - bytesReceived);

            // Handle errors
            if (ret <= 0) {
                Result_t result = (ret == HTTPD_SOCK_ERR_TIMEOUT) ? Result_t::ReceiveTimeout : Result_t::ReceiveFail;
                return Result<CJsonPtr>::Err(result);
            }

            // Update received counter
            bytesReceived += static_cast<size_t>(ret);
        }
        // Ensure null-termination
        postBuff.get()[req->content_len] = '\0';  

        // Parse out JSON payload
        CJsonPtr json(cJSON_Parse(postBuff.get()), &cJSON_Delete);
        if (!json)
            return Result<CJsonPtr>::Err(Result_t::ParseFail);

        // Return success
        return Result<CJsonPtr>::Ok(std::move(json));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
    }
}

Result_t CWebServer::SendHttpResponseNoBody(httpd_req_t *req, HttpStatus_t httpStatus) {
    // Sanity check
    if (!req)
        return Result_t::InvalidArg;

    try {
        // Get the HTTP status string to use
        const std::string_view httpStatusString = GetHttpStatusString(httpStatus); // GetHttpStatusString() alyways returns a null-temrinated string

        // Set the status string
        if (httpd_resp_set_status(req, httpStatusString.data()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // No body means no Content-Type header
        if (httpd_resp_set_type(req, "") != ESP_OK)
            return Result_t::HttpSendFailure;

        // Send the response
        if (httpd_resp_send(req, nullptr, 0) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CWebServer::SendHttpResponseWithStringBody(httpd_req_t *req, HttpStatus_t httpStatus, const std::string_view body) {
    // Sanity check
    if (!req)
        return Result_t::InvalidArg;

    try {
        // Get the HTTP status string to use
        const std::string_view httpStatusString = GetHttpStatusString(httpStatus); // GetHttpStatusString() alyways returns a null-temrinated string

        // Set the status string
        if (httpd_resp_set_status(req, httpStatusString.data()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Set Content-Type
        if (httpd_resp_set_type(req, "text/plain") != ESP_OK)
            return Result_t::HttpSendFailure;

        // Send the response
        if (httpd_resp_send(req, body.data(), body.size()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CWebServer::SendHttpResponseWithJsonBody(httpd_req_t *req, HttpStatus_t httpStatus, const CJsonPtr& body) {
    // Sanity check
    if (!req || !body)
        return Result_t::InvalidArg;

    try {
        // Get the HTTP status string to use
        const std::string_view httpStatusString = GetHttpStatusString(httpStatus); // GetHttpStatusString() alyways returns a null-temrinated string

        // Set the status string
        if (httpd_resp_set_status(req, httpStatusString.data()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Get the JSON body as a string
        BufferPtr jsonString(cJSON_PrintUnformatted(body.get()), &cJSON_free);
        if (!jsonString)
            return Result_t::OutOfMemory;

        // Set Content-Type
        if (httpd_resp_set_type(req, "application/json") != ESP_OK)
            return Result_t::HttpSendFailure;

        // Send the response
        if (httpd_resp_send(req, jsonString.get(), HTTPD_RESP_USE_STRLEN) != ESP_OK)
            return Result_t::HttpSendFailure;
        
        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CWebServer::SendHttpResponseWithLocationAndJsonBody(httpd_req_t *req, HttpStatus_t httpStatus, const std::string_view location, const CJsonPtr& body) {
    // Sanity check
    if (!req || !body)
        return Result_t::InvalidArg;

    try {
        // Get the HTTP status string to use
        const std::string_view httpStatusString = GetHttpStatusString(httpStatus); // GetHttpStatusString() alyways returns a null-temrinated string

        // Set the status string
        if (httpd_resp_set_status(req, httpStatusString.data()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Set Content-Type
        if (httpd_resp_set_type(req, "application/json") != ESP_OK)
            return Result_t::HttpSendFailure;

        // Set Location
        std::string str(location);
        esp_err_t err = httpd_resp_set_hdr(req, "Location", str.c_str());
        if (err != ESP_OK)
            return Result_t::HttpSendFailure;

        // Get the JSON body as a string
        BufferPtr jsonString(cJSON_PrintUnformatted(body.get()), &cJSON_free);
        if (!jsonString)
            return Result_t::OutOfMemory;

        // Send the response
        if (httpd_resp_send(req, jsonString.get(), HTTPD_RESP_USE_STRLEN) != ESP_OK)
            return Result_t::HttpSendFailure;
        
        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CWebServer::SendHttpResponseWithProblemDetailsBody(httpd_req_t *req, HttpStatus_t httpStatus,
    const std::optional<std::string_view> problemTitle, const std::optional<std::string_view> problemDetail,
    const std::optional<std::string_view> problemInstance)
{
    // Sanity check
    if (!req)
        return Result_t::InvalidArg;

    try {
        // Get the HTTP status string to use
        const std::string_view httpStatusString = GetHttpStatusString(httpStatus); // GetHttpStatusString() alyways returns a null-temrinated string

        // Set the status string
        if (httpd_resp_set_status(req, httpStatusString.data()) != ESP_OK)
            return Result_t::HttpSendFailure;

        if (problemTitle && problemDetail && problemInstance) {
            // Problem Details specified, try and send that
            Result<CJsonPtr> problemDetails = BuildProblemDetails(problemTitle.value(), static_cast<uint16_t>(httpStatus),
                problemDetail.value(), problemInstance.value());

            if (problemDetails) {
                // Get the JSON Problem Details as a string
                std::string jsonString(cJSON_Print(problemDetails.value().get()));

                // Set Content-Type
                if (httpd_resp_set_type(req, "application/problem+json") != ESP_OK)
                    return Result_t::HttpSendFailure;

                // Send the response
                if (httpd_resp_send(req, jsonString.c_str(), HTTPD_RESP_USE_STRLEN) != ESP_OK)
                    return Result_t::HttpSendFailure;
                
                // Success
                return Result_t::OK;
            }
        }

        // Problem Details not defined, or build failed
        // Send generic response with no body

        // No body means no Content-Type
        if (httpd_resp_set_type(req, "") != ESP_OK)
            return Result_t::HttpSendFailure;

        // Send the response
        if (httpd_resp_send(req, nullptr, 0) != ESP_OK)
            return Result_t::HttpSendFailure;
    
        // Success
        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result_t::OutOfMemory;
    }
}

Result_t CWebServer::SendHttpResponseFile(httpd_req_t* req, HttpStatus_t httpStatus, const std::string_view filePath) {
    if (!req || filePath.empty())
        return Result_t::InvalidArg;

    try {
        // Create a null-terminated file path
        const std::string filePathStr(filePath);

        // Open the file in binary mode
        std::ifstream fileStream(filePathStr, std::ios::binary);
        if (!fileStream.is_open())
            return Result_t::FileNotFound;

        // Set the HTTP status
        const std::string httpStatusString(GetHttpStatusString(httpStatus));
        if (httpd_resp_set_status(req, httpStatusString.c_str()) != ESP_OK)
            return Result_t::HttpSendFailure;

        // Set Content-Type based on the file name
        const Result_t result = SetContentTypeFromFileName(req, filePath);
        if (result != Result_t::OK)
            return result;

        // Stream the file in bounded-size chunks
        std::array<char, SEND_BUFFER_SIZE> buffer{};
        do {
            fileStream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize bytesRead = fileStream.gcount();

            // Send the file chunk
            if (bytesRead > 0 && httpd_resp_send_chunk(req, buffer.data(), static_cast<ssize_t>(bytesRead)) != ESP_OK)
                return Result_t::HttpSendFailure;

            // Sanity check
            if (!fileStream.eof() && (fileStream.bad() || fileStream.fail()))
                return Result_t::FileLoadError;
        } while (!fileStream.eof());

        // Terminate the chunked HTTP response
        if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK)
            return Result_t::HttpSendFailure;

        return Result_t::OK;
    } catch (const std::bad_alloc&) {
        return Result_t::OutOfMemory;
    }
}
