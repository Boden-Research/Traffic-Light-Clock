#include <climits>
#include <array>
#include <cstdint>
#include <vector>
#include <cmath>
#include <memory>
#include <new>
#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <string.h>
#include <limits.h>

#include "WebServer.hpp"
#include "../Hardware/LightDriver.hpp"
#include "../Storage/AnimationStore.hpp"
#include "../DataTypes.hpp"
#include "../config.h"

#include "Api.hpp"

#define MAX_REQUEST_BODY_SIZE  16386
#define MAX_ANIMATION_NAME_LEN 50

// Singleton instance of CApi class, available everywhere
CApi Api;

// Procedure name strings for logging
static const char* _procStringGetAllAnimations = "GetAllAnimations";
static const char* _procStringGetAnimationByName = "GetAnimationByName";
static const char* _procStringUploadAnimation = "UploadAnimation";
static const char* _procStringDeleteAnimationByName = "DeleteAnimationByName";
static const char* _procStringPostLight = "PostLight";

// Retrieves the animation name from the HTTP request path
// Path must be "/API/animations/(name)"
// Returns a Result containing the animation name, or an error code
static Result<std::string> GetAnimationNameFromPath(const std::string_view path) {
    try {
        return Result<std::string>::Ok(std::string(path.substr(16)));
    } catch (const std::bad_alloc&) {
        return Result<std::string>::Err(Result_t::OutOfMemory);
    }
}

// Sends an HTTP error response to the client
// Returns a Result_t result code
static bool SendErrorResponse(httpd_req_t *req, Result_t result,
    const std::string_view requestPath, const std::string_view procedure)
{
    const std::string_view resultString = GetResultDescription(result);

    switch (result) {
        case Result_t::Failed:
        case Result_t::InvalidArg:
        case Result_t::OutOfMemory:
        case Result_t::FileLoadError:
        case Result_t::FileSaveError:
        case Result_t::NoResources:
        case Result_t::InvalidState:
        case Result_t::FileLoadBadFile:
        case Result_t::FileLoadEOF:
        case Result_t::BufferTooSmall:
        case Result_t::OutOfStorageSpace:
            ESP_LOGE(CApi::TAG, "%.*s: Error: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
                resultString) == Result_t::OK;
        
        case Result_t::ParseFail:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Parse fail", resultString, requestPath) == Result_t::OK;

        case Result_t::ParamNotFound:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Param missing", resultString, requestPath) == Result_t::OK;

        case Result_t::RequestTooBig:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Request too big", resultString, requestPath) == Result_t::OK;

        case Result_t::RequestBodyEmpty:
        case Result_t::InvalidJson:
        case Result_t::FileBadName:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP422UnprocessableContent,
                "Invalid entitiy", resultString, requestPath) == Result_t::OK;

        case Result_t::ReceiveTimeout:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP408RequestTimeout) == Result_t::OK;
        
        case Result_t::ReceiveFail:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            break;

        case Result_t::FileNotFound:
            ESP_LOGW(CApi::TAG, "%.*s: %.*s", procedure.size(), procedure.data(),
                resultString.size(), resultString.data());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;

        case Result_t::HttpSendFailure:
            // Nothing we can do here...
            break;

        default:
            // A kind of error that isn't send back to caller
            break;

    }

    return true;
}

// API endpoint handler for GET "/API/animations"
static bool ApiEndpointGetAllAnimations(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        // Create response JSON array
        CJsonPtr json(cJSON_CreateArray(), &cJSON_Delete);
        if (!json)
            return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);

        // Start iteration
        Result<AnimationStoreIterator> iterator = AnimationStore.OpenAnimationIterator();
        if (!iterator)
            return SendErrorResponse(req, iterator.error(), path, _procStringGetAllAnimations);

        // Iterate over available animations, adding them to the JSON array
        std::string animationName;
        Result_t result;
        while (AnimationStore.NextAnimation(iterator.value(), animationName, result)) {
            // Handle iterator errors
            if (result != Result_t::OK)
                return SendErrorResponse(req, result, path, _procStringGetAllAnimations);

            // Load the animation
            Result<LightAnimationPtr> animation = AnimationStore.Load(animationName);
            if (!animation)
                // Failed to load; ignore and continue
                continue;

            // Serialize the animation into a JSON object
            Result<CJsonPtr> animationJson = SerializeLightAnimation(animationName, *animation.value().get());
            if (!animationJson)
                // Serialization failed; ignore and continue
                continue;

            // Add animation to output
            if (!cJSON_AddItemToArray(json.get(), animationJson.value().get()))
                return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
            animationJson.value().release();
        }

        // Send response to client
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, json) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
    }
}

// API endpoint handler for GET "/API/animations/*"
static bool ApiEndpointGetAnimationByName(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        // Get the animation name from the request path
        Result<std::string> name = GetAnimationNameFromPath(path);
        if (!name)
            return SendErrorResponse(req, name.error(), path, _procStringGetAnimationByName);

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(name.value())) {
            ESP_LOGW(CApi::TAG, "%s: Invalid animation name: %s", _procStringGetAnimationByName, name.value().c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }

        // Load the animation
        Result<LightAnimationPtr> animation = AnimationStore.Load(name.value());
        if (!animation)
            return SendErrorResponse(req, animation.error(), path, _procStringGetAnimationByName);

        Result<CJsonPtr> animationJson = SerializeLightAnimation(name.value(), *animation.value().get());
        if (!animationJson)
            return SendErrorResponse(req, animationJson.error(), path, _procStringGetAnimationByName);

        // Send response to client
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, animationJson.value()) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
    }
}

// API endpoint handler for POST "/API/animations"
static bool ApiEndpointUploadAnimation(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        // Read in the entire request body
        Result<CJsonPtr> json = CWebServer::ReceiveRequestJsonBody(req);
        if (!json)
            return SendErrorResponse(req, json.error(), path, _procStringUploadAnimation);

        // Extract the animation name
        cJSON* jsonName = cJSON_GetObjectItem(json.value().get(), "Name");
        if (!jsonName || !cJSON_IsString(jsonName))
            return SendErrorResponse(req, Result_t::InvalidJson, path, _procStringUploadAnimation);

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(jsonName->string))
            return SendErrorResponse(req, Result_t::FileBadName, path, _procStringUploadAnimation);

        // Parse the animation
        Result<LightAnimationPtr> animation = ParseLightAnimation(*json.value().get());
        if (!animation)
            return SendErrorResponse(req, animation.error(), path, _procStringUploadAnimation);

        // Determine if the animation already exists
        Result<bool> replaced = AnimationStore.Exists(jsonName->string);
        if (!replaced)
            return SendErrorResponse(req, replaced.error(), path, _procStringUploadAnimation);

        // Save the animation
        Result_t result = AnimationStore.Save(jsonName->string, animation.value());
        if (result != Result_t::OK)
            return SendErrorResponse(req, result, path, _procStringUploadAnimation);

        // Reserialize the animation for output
        Result<CJsonPtr> response = SerializeLightAnimation(jsonName->string, *animation.value());
        if (!response)
            return SendErrorResponse(req, response.error(), path, _procStringUploadAnimation);

        if (replaced)
            // Animation was replaced
            return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, response.value()) == Result_t::OK;
        else {
            // Animation was created
            // Construct location where animation was created
            std::string location = "/API/animations/" + std::string(jsonName->string);

            // Send response to caller
            return CWebServer::SendHttpResponseWithLocationAndJsonBody(req, HttpStatus_t::HTTP201Created, location, response.value()) == Result_t::OK;
        }
    } catch (const std::bad_alloc&) {
        return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
    }
}

// API endpoint handler for DELETE "/API/animations/*"
static bool ApiEndpointDeleteAnimationByName(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        // Get the animation name from the request path
        Result<std::string> name = GetAnimationNameFromPath(path);
        if (!name)
            return SendErrorResponse(req, name.error(), path, _procStringGetAnimationByName);

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(name.value())) {
            ESP_LOGW(CApi::TAG, "%s: Invalid animation name: %s", _procStringDeleteAnimationByName, name.value().c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }

        // Try to delete the animation
        Result_t result = AnimationStore.Delete(name.value());
        if (result != Result_t::OK)
            return SendErrorResponse(req, result, path, _procStringDeleteAnimationByName);

        // Send response
        return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP204NoContent) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
    }
}

// API endpoint handler for POST "/API/light"
static bool ApiEndpointPostLight(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        // Read in the entire request body
        Result<CJsonPtr> json = CWebServer::ReceiveRequestJsonBody(req);
        if (!json)
            return SendErrorResponse(req, json.error(), path, _procStringUploadAnimation);

        // Get the "outputs" array
        // We can declare this here because we only need to delete the json, not any objects retrieved from it
        cJSON* outputsArray = cJSON_GetObjectItem(json.value().get(), "outputs");
        if (outputsArray == NULL || !cJSON_IsArray(outputsArray))
            return SendErrorResponse(req, Result_t::InvalidJson, path, _procStringPostLight);

        Result<LightStatePtr> lightState = ParseLightState(*outputsArray);
        if (!lightState)
            return SendErrorResponse(req, lightState.error(), path, _procStringUploadAnimation);

        // Send the updated state to the driver
        LightDriver.Update(*lightState.value().get());

        // Craft response
        CJsonPtr response(cJSON_CreateObject(), &cJSON_Delete);
        if (!response)
            return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
        if (!cJSON_AddStringToObject(response.get(), "Status", "OK"))
            return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);

        // Send the response
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, response) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        return SendErrorResponse(req, Result_t::OutOfMemory, path, _procStringGetAllAnimations);
    }
}

Result_t CApi::Initialize() {
    Result_t result;

    // Register API endpoints
    result = WebServer.RegisterAPIEndpoint("/API/lights", HTTP_POST, ApiEndpointPostLight);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/animations", HTTP_GET, ApiEndpointGetAllAnimations);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/animations/*", HTTP_GET, ApiEndpointGetAnimationByName);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/animations", HTTP_POST, ApiEndpointUploadAnimation);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/animations", HTTP_PUT, ApiEndpointUploadAnimation);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/animations/*", HTTP_DELETE, ApiEndpointDeleteAnimationByName);
    if (result != Result_t::OK)
        return result;

    return Result_t::OK;
}
