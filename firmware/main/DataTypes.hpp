#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string_view>
#include <memory>
#include <filesystem>

#include <limits.h>
#include <cJSON.h>
#include <esp_err.h>

#include "config.h"

/// @brief Enum for returning results from functions.
enum class Result_t : uint8_t {
    OK,                 // No error
    Failed,             // Non-specific failure
    InvalidArg,         // An invalid arg was passed to function
    RequestBodyEmpty,   // Request body was empty
    RequestTooBig,      // HTTP request body too big to process
    OutOfMemory,        // No memory left
    ReceiveTimeout,     // HTTP request body receive timeout
    ReceiveFail,        // General failure during HTTP receive
    ParseFail,          // Failed to parse a HTTP request body
    ParamNotFound,      // Query param not supplied
    InvalidJson,        // JSON structure or value not acceptable
    FileNotFound,       // Animation was not found
    FileLoadError,      // Animation failed to load
    FileSaveError,      // Animation failed to save
    FileBadName,        // Animation name contains illegal characters or is too long
    NoResources,        // Resource exhaustion encountered
    InvalidState,       // Invalid state
    FileLoadBadFile,    // Tried to load a file with a bad structure
    FileLoadEOF,        // Unexpected EOF when loading file
    BufferTooSmall,     // Buffer to receive values is too small for the data
    OutOfStorageSpace,  // There is not enough free space on the file system
    HttpSendFailure,    // Failed to send HTTP response
    NoSlotsFree,        // There are no more slots free
    EntryNotFound,      // Entry was not found
    InitFlashFailed,    // Initialization of NVS failed
    InitWiFiFailed,     // Failed to initialize WiFi
    InitLightFailed,    // Failed to initialize light driver
};

constexpr std::size_t LIGHT_STATE_NUM_BYTES = ((NUM_LIGHT_OUTPUTS + CHAR_BIT - 1) / CHAR_BIT); // Evaluates to 11

using LightState_t = std::array<uint8_t, LIGHT_STATE_NUM_BYTES>;

struct LightAnimation_t {
    float FrameRate;
    std::vector<LightState_t> Frames;
};

// Iterator type for iterating over available animation names
struct AnimationStoreIterator_t
{
    std::filesystem::directory_iterator Current;
    std::filesystem::directory_iterator End;
    Result_t PendingError;
    bool Failed = false;
};

// cJSON RAII type
using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// char RAII type
using BufferPtr = std::unique_ptr<char, decltype(&std::free)>;

// LightState_t RAII type
using LightStatePtr = std::unique_ptr<LightState_t>;

// LightAnimation_t RAII type
using LightAnimationPtr = std::unique_ptr<LightAnimation_t>;

// AnimationStoreIterator_t RAII type
using AnimationStoreIterator = std::unique_ptr<AnimationStoreIterator_t>;

/// @brief Template class for returning either a successful result value from a function, or a Result_t error value.
/// @tparam T The successful result type.
template<typename T>
class Result {
    public:
        // Construct a successful result
        [[nodiscard]]
        static Result Ok(T value) {
            return Result(std::in_place_index<0>, std::move(value));
        }

        // Construct a failed result
        [[nodiscard]]
        static Result Err(Result_t value) {
            assert(value != Result_t::OK);
            return Result(std::in_place_index<1>, value);
        }

        [[nodiscard]]
        bool has_value() const noexcept {
            return _data.index() == 0;
        }

        [[nodiscard]]
        explicit operator bool() const noexcept {
            return has_value();
        }

        // Access the successful value.
        T& value() & {
            assert(has_value());
            return std::get<0>(_data);
        }

        const T& value() const & {
            assert(has_value());
            return std::get<0>(_data);
        }

        T&& value() && {
            assert(has_value());
            return std::get<0>(std::move(_data));
        }

        // Access the error.
        Result_t error() const & {
            assert(!has_value());
            return std::get<1>(_data);
        }

    private:
        // Private constructor, initializes the _data variable
        template <std::size_t I, typename U>
        explicit Result(std::in_place_index_t<I> tag, U&& value)
            : _data(tag, std::forward<U>(value)) {}

        std::variant<T, Result_t> _data;
};

/// @brief Function that finds the appropriate error description for logging.
/// @param result (in) The Result_t to look up
/// @return A std:string_view containing the error description.
const std::string_view GetResultDescription(Result_t result);

/// @brief Creates an empty (or "null") CJsonPtr.
/// @return An empty CJsonPtr.
CJsonPtr EmptyCJsonPtr();

/// @brief Creates an empty (or "null") BufferPtr.
/// @return An empty BufferPtr.
BufferPtr EmptyBufferPtr();

/// @brief Converts an esp_err_t into a Result_t value.
/// @param err (in) The error value to convert.
/// @return The converter result value.
Result_t EspErrToResult(esp_err_t err);

/// @brief Parses a JSON array into a LightStatePtr.
/// @param json (in) The JSON array to parse.
/// @return A Result containing a LightStatePtr, or an error code.
Result<LightStatePtr> ParseLightState(const cJSON& json);

/// @brief Serializes a LightState_t stucture into a JSON array.
/// @param state (in) The structure to serialize.
/// @return A Result containing a CJsonPtr, or an error code.
Result<CJsonPtr> SerializeLightState(const LightState_t& state);

/// @brief Parses a JSON object into a LightAnimationPtr.
/// @param json (in) The JSON object to parse.
/// @return A Result containing a LightAnimationPtr, or an error code.
Result<LightAnimationPtr> ParseLightAnimation(const cJSON& json);

/// @brief Serializes a LightAnimation_t structure into a JSON object.
/// @param name (in) The name of the animation to include in the serialization.
/// @param animation (in) The animation to serialize.
/// @return A Result containing a CJsonPtr, or an error code.
Result<CJsonPtr> SerializeLightAnimation(const std::string_view name, const LightAnimation_t& animation);
