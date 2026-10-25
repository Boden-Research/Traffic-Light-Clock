#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_err.h>

#include "../DataTypes.hpp"
#include "../DataTypes.hpp"

/// @brief Class for driving the traffic lights.
class CLightDriver {
    public:
        static constexpr const char* TAG = "CLightDriver";

        /// @brief Initializes the light driver with the given parameters.
        /// @param dataPin The GPIO pin connected to the SER_IN (serial data) pin.
        /// @param clkPin The GPIO pin connected to the SRCK (serial clock) pin.
        /// @param loadPin The GPIO pin connected to the RCK (register clock) pin.
        /// @param numModules Number of 74HC595/TPIC6B595 IO expansion modules connected.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t Initialize(int dataPin, int clkPin, int loadPin, int numModules);

        /// @brief Sends an update to the connected IO expansion modules, updating all the outputs in one go.
        /// @param state Pointer to a LightState_t type describing the state to set.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t Update(LightState_t& state);

        /// @brief Clears all outputs on all IO expansion modules.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t Clear();

    private:
        spi_device_handle_t _spiHandle; // Handle for the SPI hardware interface
        
        gpio_num_t _dataPin; // Which pin is used for "DATA"
        gpio_num_t _clkPin; // Which pin is used for "CLK"
        gpio_num_t _loadPin; // Which pin is used for "LOAD"

        int _numModules; // Number of 74HC595/TPIC6B595 IO expansion modules connected
};

// Make the static LightDriver instance globally available
extern CLightDriver LightDriver;
