#include <memory>
#include <string_view>
#include <cmath>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_http_server.h>

#include "DataTypes.hpp"
#include "config.h"

static const char* TAG = "DataTypes";

const std::string_view GetResultDescription(Result_t result) {
    switch (result) {
        case Result_t::OK:                  return "No error";
        case Result_t::Failed:              return "Non-specific failure";
        case Result_t::InvalidArg:          return "An invalid arg was passed to function";
        case Result_t::RequestBodyEmpty:    return "Request body was empty";
        case Result_t::RequestTooBig:       return "HTTP request body too big to process";
        case Result_t::OutOfMemory:         return "No memory left";
        case Result_t::ReceiveTimeout:      return "HTTP request body receive timeout";
        case Result_t::ReceiveFail:         return "General failure during receive";
        case Result_t::ParseFail:           return "Failed to parse request body";
        case Result_t::ParamNotFound:       return "Query param not supplied";
        case Result_t::InvalidJson:         return "JSON structure or value not acceptable";
        case Result_t::FileNotFound:        return "File was not found";
        case Result_t::FileLoadError:       return "File failed to load";
        case Result_t::FileSaveError:       return "File failed to save";
        case Result_t::FileBadName:         return "File name contains illegal characters, is too long, or is otherwise unacceptable";
        case Result_t::NoResources:         return "Resource exhaustion encountered";
        case Result_t::InvalidState:        return "Invalid state";
        case Result_t::FileLoadBadFile:     return "Tried to load a file with a bad structure";
        case Result_t::FileLoadEOF:         return "Unexpected EOF when loading file";
        case Result_t::BufferTooSmall:      return "Buffer to receive values is too small for the data";
        case Result_t::OutOfStorageSpace:   return "There is not enough free space on the file system";
        case Result_t::HttpSendFailure:     return "Failed to send HTTP response";
        case Result_t::NoSlotsFree:         return "There are no more slots free";
        case Result_t::EntryNotFound:       return "Entry was not found";
        case Result_t::InitFlashFailed:     return "Initialization of NVS failed";
        case Result_t::InitWiFiFailed:      return "Failed to initialize WiFi";
        case Result_t::InitLightFailed:     return "Failed to initialize light driver";
        default:                            return "Unknown result code";
    }
}

CJsonPtr EmptyCJsonPtr() {
    return CJsonPtr(nullptr, &cJSON_Delete);
}

BufferPtr EmptyBufferPtr() {
    return BufferPtr(nullptr, &std::free);
}

Result_t EspErrToResult(esp_err_t err) {
    switch (err) {
        case ESP_OK:
             return Result_t::OK;

        case ESP_ERR_NO_MEM:
        case ESP_ERR_HTTPD_ALLOC_MEM:
            return Result_t::OutOfMemory;

        case ESP_ERR_INVALID_ARG:
            return Result_t::InvalidArg;

        case ESP_ERR_INVALID_STATE:
            return Result_t::InvalidState;

        case ESP_ERR_NOT_FOUND:
            return Result_t::FileNotFound;

        default:
            return Result_t::Failed;

    };
}

Result<LightStatePtr> ParseLightState(const cJSON& json) {
    // Sanity checks
    if (!cJSON_IsArray(&json)) {
        ESP_LOGE(TAG, "Expected JSON array for light state");
        return Result<LightStatePtr>::Err(Result_t::InvalidJson);
    }
    if (cJSON_GetArraySize(&json) != LIGHT_STATE_NUM_BYTES) {
        ESP_LOGE(TAG, "Expected JSON array of size %d for light state", LIGHT_STATE_NUM_BYTES);
        return Result<LightStatePtr>::Err(Result_t::InvalidJson);
    }

    try {
        // Create the state
        LightStatePtr lightState = std::make_unique<LightState_t>();

        // Parse out the output state
        int stateIndex = 0;
        for (const cJSON* stateItem = json.child; stateItem != nullptr; stateItem = stateItem->next) {
            // Validate we're a number
            if (!cJSON_IsNumber(stateItem)) {
                ESP_LOGE(TAG, "Expected number in light state array");
                return Result<LightStatePtr>::Err(Result_t::InvalidJson);
            }

            // Get the value
            const double value = stateItem->valuedouble;

            // Validate the value
            if (!std::isfinite(value) || value < 0.0 ||
                value > 255.0 || std::floor(value) != value) {
                    ESP_LOGE(TAG, "Invalid value in light state array: %f", value);
                    return Result<LightStatePtr>::Err(Result_t::InvalidJson);
                }

            // Save the required output state at current index
            lightState->at(stateIndex++) = static_cast<uint8_t>(value);
        }

        // Success
        return Result<LightStatePtr>::Ok(std::move(lightState));
    } catch (const std::bad_alloc&) {
        ESP_LOGE(TAG, "Out of memory while parsing light state");
        return Result<LightStatePtr>::Err(Result_t::OutOfMemory);
    }
}

Result<CJsonPtr> SerializeLightState(const LightState_t& state) {
    try {
        // Create array for frame
        CJsonPtr output(cJSON_CreateArray(), &cJSON_Delete);
        if (!output)
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

        // Serialize the light state numbers
        for (uint8_t i = 0; i < LIGHT_STATE_NUM_BYTES; i++) {
            // Create number for this light state
            CJsonPtr lightStates(cJSON_CreateNumber(state.at(i)), &cJSON_Delete);
            if (!lightStates) 
                return Result<CJsonPtr>::Err(Result_t::OutOfMemory);

            // Add the light state byte to the frame
            if (!cJSON_AddItemToArray(output.get(), lightStates.get()))
                return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
            lightStates.release();
        }

        // Success
        return Result<CJsonPtr>::Ok(std::move(output));
    } catch (const std::bad_alloc&) {
        ESP_LOGE(TAG, "Out of memory while serializing light state");
        return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
    }
}

Result<LightAnimationPtr> ParseLightAnimation(const cJSON& json) {
    // Sanity checks
    if (!cJSON_IsObject(&json)) {
        ESP_LOGE(TAG, "Expected JSON object for light animation");
        return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
    }

    try {
        // Parse out FrameRate element
        const cJSON* frameRate = cJSON_GetObjectItemCaseSensitive(&json, "FrameRate");
        if (!frameRate || !cJSON_IsNumber(frameRate) ||
            !std::isfinite(frameRate->valuedouble) ||
            frameRate->valuedouble <= 0.0 ||
            frameRate->valuedouble > std::numeric_limits<float>::max()) {
                ESP_LOGE(TAG, "Invalid FrameRate value in light animation");
                return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
            }
        
        // Convert FrameRate to float
        const float frameRateValue = static_cast<float>(frameRate->valuedouble);

        // Validate converted FrameRate
        if (!std::isfinite(frameRateValue) || frameRateValue <= 0.0) {
            ESP_LOGE(TAG, "Invalid converted FrameRate value in light animation");
            return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
        }

        // Get the Frames array
        const cJSON* frames = cJSON_GetObjectItemCaseSensitive(&json, "Frames");
        if (!frames || !cJSON_IsArray(frames)) {
            ESP_LOGE(TAG, "Expected Frames array in light animation");
            return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
        }

        // Get the number of frames
        const int numFrames = cJSON_GetArraySize(frames);
        if (numFrames <= 0 || numFrames > ANIMATION_MAX_NUM_FRAMES) {
            ESP_LOGE(TAG, "Invalid number of frames in light animation");
            return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
        }

        // Allocate animation
        LightAnimationPtr animation = std::make_unique<LightAnimation_t>();

        // Set animation parameters
        animation->FrameRate = frameRateValue;

        // Preallocate memory for all frames
        animation->Frames.reserve(numFrames);

        // Parse each frame
        const cJSON* frame;
        cJSON_ArrayForEach(frame, frames) {
            // Sanity check
            if (!cJSON_IsArray(frame) || cJSON_GetArraySize(frame) != LIGHT_STATE_NUM_BYTES) {
                ESP_LOGE(TAG, "Invalid frame in light animation");
                return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
            }

            // Create the frame state
            LightState_t parsedFrame {};

            // Parse the animation frame
            std::size_t stateIndex = 0;
            const cJSON* stateItem;
            cJSON_ArrayForEach(stateItem, frame) {
                // Validate we're a number
                if (!cJSON_IsNumber(stateItem)) {
                    ESP_LOGE(TAG, "Invalid state item in light animation");
                    return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
                }

                // Get the value
                const double value = stateItem->valuedouble;

                // Validate the value
                if (!std::isfinite(value) || value < 0.0 ||
                    value > 255.0 || std::floor(value) != value) {
                        ESP_LOGE(TAG, "Invalid state value in light animation");
                        return Result<LightAnimationPtr>::Err(Result_t::InvalidJson);
                }

                // Save the required output state at current index
                parsedFrame.at(stateIndex++) = static_cast<uint8_t>(value);
            }

            // Store the frame
            animation->Frames.push_back(parsedFrame);
        }

        // Success
        return Result<LightAnimationPtr>::Ok(std::move(animation));
    } catch (const std::bad_alloc&) {
        return Result<LightAnimationPtr>::Err(Result_t::OutOfMemory);
    }
}

Result<CJsonPtr> SerializeLightAnimation(const std::string_view name, const LightAnimation_t& animation) {
    // Sanity checks
    if (name.empty())
        return Result<CJsonPtr>::Err(Result_t::InvalidArg);
    if (!std::isfinite(animation.FrameRate) || animation.FrameRate <= 0.0f ||
        animation.Frames.empty() || animation.Frames.size() > ANIMATION_MAX_NUM_FRAMES) {
            ESP_LOGE(TAG, "Invalid animation frame rate/number of frames");
            return Result<CJsonPtr>::Err(Result_t::InvalidArg);
        }

    try {
        // Create output object to hold data
        CJsonPtr output(cJSON_CreateObject(), &cJSON_Delete);
        if (!output) {
            ESP_LOGE(TAG, "Failed to create JSON object for light animation");
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
        }

        // Serialize the animation name
        std::string str(name);
        if (!cJSON_AddStringToObject(output.get(), "Name", str.c_str())) {
            ESP_LOGE(TAG, "Failed to add animation name to JSON object");
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
        }

        // Serialize the frame rate
        if (!cJSON_AddNumberToObject(output.get(), "FrameRate", animation.FrameRate)) {
            ESP_LOGE(TAG, "Failed to add frame rate to JSON object");
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
        }

        // Create array to hold frames
        // No smart pointer, because "output" already owns it
        cJSON* frameArray = cJSON_AddArrayToObject(output.get(), "Frames");
        if (!frameArray) {
            ESP_LOGE(TAG, "Failed to create JSON array for frames");
            return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
        }

        // Serialize the frame sequence
        for (const LightState_t& state : animation.Frames) {
            // Crate array for frame
            CJsonPtr stateJson(cJSON_CreateArray(), &cJSON_Delete);
            if (!stateJson) {
                ESP_LOGE(TAG, "Failed to create JSON array for frame");
                return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
            }

            // Serialize a single frame
            for (uint8_t stateByte : state) {
                // Create number for this light state
                CJsonPtr stateItem(cJSON_CreateNumber(stateByte), &cJSON_Delete);
                if (!stateItem) {
                    ESP_LOGE(TAG, "Failed to create JSON number for light state");
                    return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
                }

                // Add the light state byte to the frame
                if (!cJSON_AddItemToArray(stateJson.get(), stateItem.get())) {
                    ESP_LOGE(TAG, "Failed to add light state to JSON array");
                    return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
                }
                stateItem.release();
            }

            // Add the frame to the output
            if (!cJSON_AddItemToArray(frameArray, stateJson.get())) {
                // Not really an OOM error as no memory is allocated, but this causes a 500 response to the caller, which is appropriate
                ESP_LOGE(TAG, "Failed to add frame to JSON array");
                return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
            }
            stateJson.release();
        }

        // Success
        return Result<CJsonPtr>::Ok(std::move(output));
    } catch (const std::bad_alloc&) {
        ESP_LOGE(TAG, "Out of memory");
        return Result<CJsonPtr>::Err(Result_t::OutOfMemory);
    }
}
