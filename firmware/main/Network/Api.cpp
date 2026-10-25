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
static const char* _procStringExecuteAnimationCommand = "ExecuteAnimationCommand";
static const char* _procStringPlaybackCustom = "PlaybackCustom";
static const char* _procStringInitialize = "Initialize";

// Name and optional first suffix component of an animation request path
struct AnimationPathParts {
    std::string Name;
    std::optional<std::string> Command;
};

// Retrieves the animation path parts from the HTTP request path
// Path must be "/API/animations/(name)[/(suffix)]"
// Returns a Result containing the animation path parts, or an error code
static Result<AnimationPathParts> GetAnimationPathPartsFromPath(const std::string_view path) {
    constexpr size_t prefixLen = 16; // Length of "/API/animations/"

    try {
        // Sanity check
        if (path.size() < prefixLen)
            return Result<AnimationPathParts>::Err(Result_t::InvalidArg);

        // Extract the part of the path after the prefix
        std::string_view rest = path.substr(prefixLen);

        // Find the end of the animation name in the path
        size_t nameEnd = rest.find('/');

        // If no slash is found, the path contains only the animation name
        AnimationPathParts parts;
        if (nameEnd == std::string_view::npos) {
            parts.Name = std::string(rest);
            return Result<AnimationPathParts>::Ok(std::move(parts));
        }

        // Extract the animation name from the path, up to the first slash
        parts.Name = std::string(rest.substr(0, nameEnd));

        // Extract the command from the rest of the path, if present
        std::string_view tail = rest.substr(nameEnd + 1);
        std::string_view suffix = tail.substr(0, tail.find('/'));
        if (!suffix.empty())
            parts.Command = std::string(suffix);

        // Return the extracted animation path parts
        return Result<AnimationPathParts>::Ok(std::move(parts));
    } catch (const std::bad_alloc&) {
        // Out of memory
        return Result<AnimationPathParts>::Err(Result_t::OutOfMemory);
    }
}

// Sends an HTTP error response to the client
// Returns a Result_t result code
static bool SendErrorResponse(httpd_req_t *req, Result_t result,
    const std::string_view requestPath)
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
            return CWebServer::SendHttpResponseWithStringBody(req, HttpStatus_t::HTTP500InternalServerError,
                resultString) == Result_t::OK;
        
        case Result_t::ParseFail:
        case Result_t::RequestBodyEmpty:
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Parse fail", resultString, requestPath) == Result_t::OK;

        case Result_t::ParamNotFound:
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Param missing", resultString, requestPath) == Result_t::OK;

        case Result_t::RequestTooBig:
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP400BadRequest,
                "Request too big", resultString, requestPath) == Result_t::OK;

        case Result_t::InvalidJson:
        case Result_t::FileBadName:
            return CWebServer::SendHttpResponseWithProblemDetailsBody(req, HttpStatus_t::HTTP422UnprocessableContent,
                "Invalid entitiy", resultString, requestPath) == Result_t::OK;

        case Result_t::ReceiveTimeout:
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP408RequestTimeout) == Result_t::OK;
        
        case Result_t::ReceiveFail:
            break;

        case Result_t::FileNotFound:
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
        ESP_LOGD(CApi::TAG, "%s: Handling request to get all animations", _procStringGetAllAnimations);

        // Create response JSON array
        CJsonPtr json(cJSON_CreateArray(), &cJSON_Delete);
        if (!json) {
            ESP_LOGE(CApi::TAG, "%s: Failed to create JSON array", _procStringGetAllAnimations);
            return SendErrorResponse(req, Result_t::OutOfMemory, path);
        }

        // Start iteration
        Result<AnimationStoreIterator> iterator = AnimationStore.OpenAnimationIterator();
        if (!iterator) {
            ESP_LOGE(CApi::TAG, "%s: Failed to open animation iterator", _procStringGetAllAnimations);
            return SendErrorResponse(req, iterator.error(), path);
        }

        // Iterate over available animations, adding them to the JSON array
        std::string animationName;
        Result_t result;
        while (AnimationStore.NextAnimation(iterator.value(), animationName, result)) {
            // Handle iterator errors
            if (result != Result_t::OK) {
                ESP_LOGE(CApi::TAG, "%s: Iterator error: %s",
                    _procStringGetAllAnimations, GetResultDescription(result).data());
                return SendErrorResponse(req, result, path);
            }

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
            if (!cJSON_AddItemToArray(json.get(), animationJson.value().get())) {
                ESP_LOGE(CApi::TAG, "%s: Failed to add animation to JSON array", _procStringGetAllAnimations);
                return SendErrorResponse(req, Result_t::OutOfMemory, path);
            }
            animationJson.value().release();
        }

        ESP_LOGD(CApi::TAG, "%s: Successfully serialized all animations", _procStringGetAllAnimations);

        // Send response to client
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, json) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringGetAllAnimations);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

// API endpoint handler for GET "/API/animations/*"
static bool ApiEndpointGetAnimationByName(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to get animation by name", _procStringGetAnimationByName);

        // Get the animation name from the request path
        Result<AnimationPathParts> animationPath = GetAnimationPathPartsFromPath(path);
        if (!animationPath) {
            ESP_LOGE(CApi::TAG, "%s: Failed to get animation name from path: %s",
                _procStringGetAnimationByName, GetResultDescription(animationPath.error()).data());
            return SendErrorResponse(req, animationPath.error(), path);
        }

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(animationPath.value().Name)) {
            ESP_LOGE(CApi::TAG, "%s: Invalid animation name: %s",
                _procStringGetAnimationByName, animationPath.value().Name.c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }

        ESP_LOGD(CApi::TAG, "%s: Animation name: %s",
            _procStringGetAnimationByName, animationPath.value().Name.c_str());

        // Load the animation
        Result<LightAnimationPtr> animation = AnimationStore.Load(animationPath.value().Name);
        if (!animation) {
            ESP_LOGE(CApi::TAG, "%s: Failed to load animation: %s",
                _procStringGetAnimationByName, GetResultDescription(animation.error()).data());
            return SendErrorResponse(req, animation.error(), path);
        }

        Result<CJsonPtr> animationJson = SerializeLightAnimation(animationPath.value().Name, *animation.value().get());
        if (!animationJson) {
            ESP_LOGE(CApi::TAG, "%s: Failed to serialize animation: %s",
                _procStringGetAnimationByName, GetResultDescription(animationJson.error()).data());
            return SendErrorResponse(req, animationJson.error(), path);
        }

        ESP_LOGD(CApi::TAG, "%s: Successfully serialized animation", _procStringGetAnimationByName);

        // Send response to client
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, animationJson.value()) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringGetAnimationByName);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

// API endpoint handler for POST "/API/animations"
static bool ApiEndpointUploadAnimation(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to upload animation", _procStringUploadAnimation);

        // Read in the entire request body
        Result<CJsonPtr> json = CWebServer::ReceiveRequestJsonBody(req);
        if (!json) {
            ESP_LOGE(CApi::TAG, "%s: Failed to receive request JSON body: %s",
                _procStringUploadAnimation, GetResultDescription(json.error()).data());
            return SendErrorResponse(req, json.error(), path);
        }

        // Extract the animation name
        cJSON* jsonName = cJSON_GetObjectItem(json.value().get(), "Name");
        if (!jsonName || !cJSON_IsString(jsonName)) {
            ESP_LOGE(CApi::TAG, "%s: Invalid JSON: Missing or invalid 'Name' field",
                _procStringUploadAnimation);
            return SendErrorResponse(req, Result_t::InvalidJson, path);
        }

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(jsonName->valuestring)) {
            ESP_LOGE(CApi::TAG, "%s: Invalid animation name: %s",
                _procStringUploadAnimation, jsonName->valuestring);
            return SendErrorResponse(req, Result_t::FileBadName, path);
        }

        ESP_LOGD(CApi::TAG, "%s: Animation name: %s",
            _procStringUploadAnimation, jsonName->valuestring);

        // Parse the animation
        Result<LightAnimationPtr> animation = ParseLightAnimation(*json.value().get());
        if (!animation) {
            ESP_LOGE(CApi::TAG, "%s: Failed to parse animation: %s",
                _procStringUploadAnimation, GetResultDescription(animation.error()).data());
            return SendErrorResponse(req, animation.error(), path);
        }

        // Determine if the animation already exists
        Result<bool> replaced = AnimationStore.Exists(jsonName->valuestring);
        if (!replaced) {
            ESP_LOGE(CApi::TAG, "%s: Failed to check if animation exists: %s",
                _procStringUploadAnimation, GetResultDescription(replaced.error()).data());
            return SendErrorResponse(req, replaced.error(), path);
        }

        // Save the animation
        Result_t result = AnimationStore.Save(jsonName->valuestring, animation.value());
        if (result != Result_t::OK) {
            ESP_LOGE(CApi::TAG, "%s: Failed to save animation: %s",
                _procStringUploadAnimation, GetResultDescription(result).data());
            return SendErrorResponse(req, result, path);
        }

        // Reserialize the animation for output
        Result<CJsonPtr> response = SerializeLightAnimation(jsonName->valuestring, *animation.value());
        if (!response) {
            ESP_LOGE(CApi::TAG, "%s: Failed to serialize animation for response: %s",
                _procStringUploadAnimation, GetResultDescription(response.error()).data());
            return SendErrorResponse(req, response.error(), path);
        }

        if (replaced.value()) {
            // Animation was replaced
            ESP_LOGD(CApi::TAG, "%s: Successfully replaced animation: %s",
                _procStringUploadAnimation, jsonName->valuestring);
            return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, response.value()) == Result_t::OK;
        } else {
            // Animation was created
            ESP_LOGD(CApi::TAG, "%s: Successfully created animation: %s",
                _procStringUploadAnimation, jsonName->valuestring);

            // Construct location where animation was created
            std::string location = "/API/animations/" + std::string(jsonName->valuestring);

            // Send response to caller
            return CWebServer::SendHttpResponseWithLocationAndJsonBody(req, HttpStatus_t::HTTP201Created, location, response.value()) == Result_t::OK;
        }
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringUploadAnimation);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

// API endpoint handler for DELETE "/API/animations/*"
static bool ApiEndpointDeleteAnimationByName(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to delete animation by name", _procStringDeleteAnimationByName);

        // Get the animation name from the request path
        Result<AnimationPathParts> animationPath = GetAnimationPathPartsFromPath(path);
        if (!animationPath) {
            ESP_LOGE(CApi::TAG, "%s: Failed to get animation name from path: %s",
                _procStringDeleteAnimationByName, GetResultDescription(animationPath.error()).data());
            return SendErrorResponse(req, animationPath.error(), path);
        }

        // Validate animation name
        if (!CAnimationStore::ValidateAnimationName(animationPath.value().Name)) {
            ESP_LOGW(CApi::TAG, "%s: Invalid animation name: %s",
                _procStringDeleteAnimationByName, animationPath.value().Name.c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }

        ESP_LOGD(CApi::TAG, "%s: Animation name: %s",
            _procStringDeleteAnimationByName, animationPath.value().Name.c_str());

        // Try to delete the animation
        Result_t result = AnimationStore.Delete(animationPath.value().Name);
        if (result != Result_t::OK) {
            ESP_LOGE(CApi::TAG, "%s: Failed to delete animation: %s",
                _procStringDeleteAnimationByName, GetResultDescription(result).data());
            return SendErrorResponse(req, result, path);
        }

        ESP_LOGD(CApi::TAG, "%s: Successfully deleted animation: %s",
            _procStringDeleteAnimationByName, animationPath.value().Name.c_str());

        // Send response
        return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP204NoContent) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringDeleteAnimationByName);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

static bool ApiEndpointExecuteAnimationCommand(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to execute animation command", _procStringExecuteAnimationCommand);

            // Get the animation name from the request path
        Result<AnimationPathParts> animationPath = GetAnimationPathPartsFromPath(path);
        if (!animationPath) {
            ESP_LOGE(CApi::TAG, "%s: Failed to get animation name from path: %s",
                _procStringExecuteAnimationCommand, GetResultDescription(animationPath.error()).data());
            return SendErrorResponse(req, animationPath.error(), path);
        }

        // Make sure the animation exists, and a valid command is provided
        if (!AnimationStore.Exists(animationPath.value().Name)) {
            ESP_LOGE(CApi::TAG, "%s: Animation does not exist: %s",
                _procStringExecuteAnimationCommand, animationPath.value().Name.c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }
        if (!animationPath.value().Command.has_value()) {
            ESP_LOGE(CApi::TAG, "%s: No command provided for animation: %s",
                _procStringExecuteAnimationCommand, animationPath.value().Name.c_str());
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
        }

        ESP_LOGD(CApi::TAG, "%s: Executing command '%s' for animation '%s'",
            _procStringExecuteAnimationCommand,
            animationPath.value().Command.value().c_str(),
            animationPath.value().Name.c_str());

        // We now have a valid animation and command, so we can execute the command
        if (animationPath.value().Command.value() == "run") {
            // Load the animation from the store
            Result<LightAnimationPtr> animation = AnimationStore.Load(animationPath.value().Name);
            if (!animation) {
                ESP_LOGE(CApi::TAG, "%s: Failed to load animation: %s",
                    _procStringExecuteAnimationCommand, GetResultDescription(animation.error()).data());
                return SendErrorResponse(req, animation.error(), path);
            }

            // Send the animation to the driver for execution
            Result_t result = LightDriver.StartAnimation(std::move(animation.value()));
            if (result != Result_t::OK) {
                ESP_LOGE(CApi::TAG, "%s: Failed to start animation: %s",
                    _procStringExecuteAnimationCommand, GetResultDescription(result).data());
                return SendErrorResponse(req, result, path);
            }

            ESP_LOGD(CApi::TAG, "%s: Successfully started animation", _procStringExecuteAnimationCommand);
            return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP202Accepted) == Result_t::OK;
        }

        // Return error for unsupported commands
        ESP_LOGE(CApi::TAG, "%s: Unsupported command for animation: %s",
            _procStringExecuteAnimationCommand, animationPath.value().Command.value().c_str());
        return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP404NotFound) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringExecuteAnimationCommand);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

// Playback custom API endpoint handler for POST "/API/playback/custom"
static bool ApiEndpointPlaybackCustom(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to play custom animation", _procStringPlaybackCustom);

        // Read in the entire request body
        Result<CJsonPtr> json = CWebServer::ReceiveRequestJsonBody(req);
        if (!json) {
            ESP_LOGE(CApi::TAG, "%s: Failed to receive request JSON body: %s",
                _procStringPlaybackCustom, GetResultDescription(json.error()).data());
            return SendErrorResponse(req, json.error(), path);
        }

        // Parse the animation
        Result<LightAnimationPtr> animation = ParseLightAnimation(*json.value().get());
        if (!animation) {
            ESP_LOGE(CApi::TAG, "%s: Failed to parse animation: %s",
                _procStringPlaybackCustom, GetResultDescription(animation.error()).data());
            return SendErrorResponse(req, animation.error(), path);
        }

        // Send the playback request to the driver
        Result_t result = LightDriver.StartAnimation(std::move(animation.value()));
        if (result != Result_t::OK) {
            ESP_LOGE(CApi::TAG, "%s: Failed to start animation: %s",
                _procStringPlaybackCustom, GetResultDescription(result).data());
            return SendErrorResponse(req, result, path);
        }

        ESP_LOGD(CApi::TAG, "%s: Successfully started animation", _procStringPlaybackCustom);

        // Send response
        return CWebServer::SendHttpResponseNoBody(req, HttpStatus_t::HTTP204NoContent) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringPlaybackCustom);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

// API endpoint handler for POST "/API/light"
static bool ApiEndpointPostLight(httpd_req_t *req, const std::string& path,
    const std::optional<std::string>& query, const std::optional<std::string>& fragment)
{
    try {
        ESP_LOGD(CApi::TAG, "%s: Handling request to update light state", _procStringPostLight);

        // Read in the entire request body
        Result<CJsonPtr> json = CWebServer::ReceiveRequestJsonBody(req);
        if (!json) {
            ESP_LOGE(CApi::TAG, "%s: Failed to receive request JSON body: %s", _procStringPostLight, GetResultDescription(json.error()).data());
            return SendErrorResponse(req, json.error(), path);
        }

        // Get the "outputs" array
        // We can declare this here because we only need to delete the json, not any objects retrieved from it
        cJSON* outputsArray = cJSON_GetObjectItem(json.value().get(), "outputs");
        if (outputsArray == NULL || !cJSON_IsArray(outputsArray)) {
            ESP_LOGE(CApi::TAG, "%s: Missing or invalid 'outputs' array in JSON body", _procStringPostLight);
            return SendErrorResponse(req, Result_t::InvalidJson, path);
        }

        Result<LightStatePtr> lightState = ParseLightState(*outputsArray);
        if (!lightState) {
            ESP_LOGE(CApi::TAG, "%s: Failed to parse light state: %s",
                _procStringPostLight, GetResultDescription(lightState.error()).data());
            return SendErrorResponse(req, lightState.error(), path);
        }

        // Send the updated state to the driver
        LightDriver.Update(*lightState.value().get());

        ESP_LOGI(CApi::TAG, "%s: Successfully updated light state", _procStringPostLight);

        // Craft response
        CJsonPtr response(cJSON_CreateObject(), &cJSON_Delete);
        if (!response) {
            ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringPostLight);
            return SendErrorResponse(req, Result_t::OutOfMemory, path);
        }
        if (!cJSON_AddStringToObject(response.get(), "Status", "OK")) {
            ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringPostLight);
            return SendErrorResponse(req, Result_t::OutOfMemory, path);
        }

        // Send the response
        return CWebServer::SendHttpResponseWithJsonBody(req, HttpStatus_t::HTTP200OK, response) == Result_t::OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(CApi::TAG, "%s: Out of memory", _procStringPostLight);
        return SendErrorResponse(req, Result_t::OutOfMemory, path);
    }
}

Result_t CApi::Initialize() {
    Result_t result;

    ESP_LOGD(CApi::TAG, "%s: Initializing API", _procStringInitialize);

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

    result = WebServer.RegisterAPIEndpoint("/API/animations/*", HTTP_POST, ApiEndpointExecuteAnimationCommand);
    if (result != Result_t::OK)
        return result;

    result = WebServer.RegisterAPIEndpoint("/API/playback/custom", HTTP_POST, ApiEndpointPlaybackCustom);
    if (result != Result_t::OK)
        return result;

    ESP_LOGD(CApi::TAG, "%s: Finished registering API endpoints", _procStringInitialize);

    return Result_t::OK;
}
