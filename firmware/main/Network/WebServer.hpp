#pragma once

#include <string_view>

#include <inttypes.h>
#include <esp_err.h>
#include <esp_http_server.h>

#include "../DataTypes.hpp"

// Maximum number of registered API endpoint handlers
#define MAX_WEB_SERVER_API_HANDLERS 64

/// @brief API handler typedef. Return false to immediately abort connection.
typedef bool (*WebServerApiHandler_t)(httpd_req_t* req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment);

typedef struct {
    const char* Path;
    http_method Method;
    WebServerApiHandler_t Handler;
} WebServerApiHandlerRegistration_t;

/// @brief Enum for defining HTTP status codes
enum class HttpStatus_t : uint16_t {
    /// @brief Standard response for successful HTTP requests.
    HTTP200OK = 200,
    /// @brief The request has been fulfilled, resulting in the creation of a new resource.
    HTTP201Created = 201,
    /// @brief The request has been accepted for processing, but the processing has not been completed.
    HTTP202Accepted = 202,
    /// @brief The server successfully processed the request, but is not returning any content.
    HTTP204NoContent = 204,
    /// @brief This and all future requests should be directed to the given URI.
    HTTP301MovedPermanently = 301,
    /// @brief Tells the client to look at another URL temporarily (historically "Moved Temporarily").
    HTTP302Found = 302,
    /// @brief Indicates that the resource has not been modified since the version specified by the request headers.
    HTTP304NotModified = 304,
    /// @brief The server cannot or will not process the request due to an apparent client error.
    HTTP400BadRequest = 400,
    /// @brief Similar to 403 Forbidden but specifically for use when authentication is required and has failed or has not yet been provided.
    HTTP401Unauthorized = 401,
    /// @brief The request was valid, but the server is refusing action. The user might not have the necessary permissions for a resource.
    HTTP403Forbidden = 403,
    /// @brief The requested resource could not be found but may be available in the future.
    HTTP404NotFound = 404,
    /// @brief A request method is not supported for the requested resource (e.g., GET vs POST).
    HTTP405MethodNotAllowed = 405,
    /// @brief The requested resource is capable of generating only content not acceptable according to the Accept headers sent in the request.
    HTTP406NotAcceptable = 406,
    /// @brief The server timed out waiting for the request.
    HTTP408RequestTimeout = 408,
    /// @brief The request is larger than the server is willing or able to process.
    HTTP413PayloadTooLarge = 413,
    /// @brief The URI provided was too long for the server to process.
    HTTP414URITooLong = 414,
    /// @brief The request entity has a media type which the server or resource does not support.
    HTTP415UnsupportedMediaType = 415,
    /// @brief WebDAV: The request was well-formed but was unable to be followed due to semantic errors.
    HTTP422UnprocessableContent = 422,
    /// @brief The user has sent too many requests in a given amount of time ("rate limiting").
    HTTP429TooManyRequests = 429,
    /// @brief A generic error message, given when an unexpected condition was encountered and no more specific message is suitable.
    HTTP500InternalServerError = 500,
    /// @brief The server cannot handle the request (because it is overloaded or down for maintenance).
    HTTP503ServiceUnavailable = 503,
};

/// @brief Singleton class for providing an HTTP server implementation with API endpoint callbacks.
class CWebServer {
    public:
        static constexpr const char* TAG = "CWebServer";

        /// @brief Initializes the web server.
        /// @return Any error, or ESP_OK.
        Result_t Initialize();

        /// @brief Registers an HTTP API endpoint handler.
        /// @param uri The URI to host this endpoint on. Must start with a forward slash. Caller may free() string once registered.
        /// @param method The HTTP method to respond to. Values: HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE.
        /// @param handler The callback to run to handle the request.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t RegisterAPIEndpoint(const char* path, http_method method, WebServerApiHandler_t handler);

        /// @brief Unregisters an HTTP API endpoint handler.
        /// @param uri The URI this endpoint is hosted on. Must start with a forward slash.
        /// @param method The HTTP method the handler responds to. Values: HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t UnregisterAPIEndpoint(const char* path, http_method method);

        /// @brief Decodes a string that was URL encoded.
        /// @param source (in) The string to decode.
        /// @return A Result containing the decoded string, or an error code.
        /// @warning It is possible for the output string to contain null characters.
        static Result<std::string> UrlDecode(const std::string_view source);

        /// @brief Gets the value of a query parameter.
        /// @param queryString (in) The full query string to parse.
        /// @param keyName (in) The query parameter key to search for.
        /// @return A Result containing the URL-decoded query string value, or an error code.
        static Result<std::string> GetQueryParam(const std::string_view queryString, const std::string& keyName);

        /// @brief Reads the entire HTTP request body as a JSON object.
        /// @param req (int) The HTTP request.
        /// @return A Result containing the JSON object, or an error code.
        static Result<CJsonPtr> ReceiveRequestJsonBody(httpd_req_t *req);

        /// @brief Sends an HTTP response to the caller with no body and no Content-Type header.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseNoBody(httpd_req_t *req, HttpStatus_t httpStatus);

        /// @brief Sends an HTTP response to the caller with a "text/plain" Content-Type header and a plain text body.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @param body (in) The response body to return.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseWithStringBody(httpd_req_t *req, HttpStatus_t httpStatus, const std::string_view body);

        /// @brief Sends an HTTP response to the caller with an "application/json" Content-Type header and a JSON body.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @param body (in) The response body to return.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseWithJsonBody(httpd_req_t *req, HttpStatus_t httpStatus, const CJsonPtr& body);

        /// @brief Sends an HTTP response to the caller with an "application/json" Content-Type header, Location header and a JSON body.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @param location (in) The value to set for the Location header.
        /// @param body (in) The response body to return.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseWithLocationAndJsonBody(httpd_req_t *req, HttpStatus_t httpStatus, const std::string_view location, const CJsonPtr& body);

        /// @brief Sends an HTTP response to the caller with an "application/problem+json" Content-Type header and a JSON Problem Details body.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @param problemTitle (in) The title string of the Problem Details payload.
        /// @param problemDetail (in) The detail string of the Problem Details payload.
        /// @param problemInstance (in) The instance string of the Problem Details payload.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseWithProblemDetailsBody(httpd_req_t *req, HttpStatus_t httpStatus,
            const std::optional<std::string_view> problemTitle, const std::optional<std::string_view> problemDetail,
            const std::optional<std::string_view> problemInstance);

        /// @brief Sends a file from the VSF file system to the caller. Automatically sets the Content-Type header from the file extension.
        /// @param req (in) The HTTP request.
        /// @param httpStatus (in) The HTTP status code to respond with.
        /// @param filePath (in) The full path to the file to send.
        /// @return A result code indicating success, or any error encountered.
        static Result_t SendHttpResponseFile(httpd_req_t* req, HttpStatus_t httpStatus, const std::string_view filePath);

    private:
        // Storage for web API handler definitions
        WebServerApiHandlerRegistration_t __webApiHandlerRegistrations[MAX_WEB_SERVER_API_HANDLERS] = {};

        // Finds a handler for the given request path
        WebServerApiHandlerRegistration_t* FindHandler(httpd_req_t* req, const std::string_view path);

        // Handler for static files, loads from SPIFFS
        static esp_err_t HttpRequestHandler(httpd_req_t *req);
    
};

// Make the static WebServer instance globally available
extern CWebServer WebServer;
