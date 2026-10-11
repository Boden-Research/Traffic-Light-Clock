#pragma once

#include <esp_err.h>

/// @brief Singleton class for handing API requests from the website.
class CApi {
    public:
        static constexpr const char* TAG = "CApi";

        /// @brief Initializes the API handler, registering the required API endpoints.
        /// @return 
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t Initialize();

};

// Make the static Api instance globally available
extern CApi Api;
