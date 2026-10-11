#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_err.h>

class CLightDriver {
    public:
        static constexpr const char* TAG = "CLightDriver";

        /// @brief Initializes the light driver with the given parameters.
        /// @param dataPin The GPIO pin connected to the SER_IN (serial data) pin.
        /// @param clkPin The GPIO pin connected to the SRCK (serial clock) pin.
        /// @param loadPin The GPIO pin connected to the RCK (register clock) pin.
        /// @param numModules Number of 74HC595/TPIC6B595 IO expansion modules connected.
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t Initialize(int dataPin, int clkPin, int loadPin, int numModules);

        /// @brief Sends an update to the connected IO expansion modules, updating all the outputs in one go.
        /// @param states Pointer to an array of output state to set, one bit per state. Array must be big enough to update all configured modules.
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t Update(uint8_t* states);

        /// @brief Clears all outputs on all IO expansion modules.
        /// @return ESP_OK on success, otherwise returns an error code.
        esp_err_t Clear();

    private:
        // Handle for the SPI hardware interface
        spi_device_handle_t _spiHandle;

        // Number of 74HC595/TPIC6B595 IO expansion modules connected
        int _numModules;
};

// Make the static LightDriver instance globally available
extern CLightDriver LightDriver;
