#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_err.h>

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
        /// @return The result code indicating success or failure reason.
        Result_t Initialize(int dataPin, int clkPin, int loadPin, int numModules);

        /// @brief Sends an update to the connected IO expansion modules, updating all the outputs in one go.
        /// @param state Reference to a LightState_t type describing the state to set.
        /// @return The result code indicating success or failure reason.
        Result_t Update(const LightState_t& state);

        /// @brief Clears all outputs on all IO expansion modules.
        /// @return The result code indicating success or failure reason.
        Result_t Clear();

        /// @brief Starts playing an animation once on the traffic light display. This procedure must take ownership of the animation object.
        /// @param animation The animation to play. Ownership of the animation object must be transferred by the caller to the light driver.
        /// @return The result code indicating success or failure reason.
        Result_t StartAnimation(LightAnimationPtr animation);

    private:
        // Variables for the light driver configuration and handles
        gpio_num_t _dataPin; // Which pin is used for "DATA"
        gpio_num_t _clkPin; // Which pin is used for "CLK"
        gpio_num_t _loadPin; // Which pin is used for "LOAD"
        int _numModules; // Number of 74HC595/TPIC6B595 IO expansion modules connected
        spi_device_handle_t _spiHandle = nullptr; // Handle for the SPI hardware interface
        esp_timer_handle_t _animationTimer = nullptr;
        SemaphoreHandle_t _animationMutex = nullptr;

        // Variables for managing the animation playback
        LightAnimationPtr _animation;
        int64_t _animationStartUs = 0;
        bool _animationPlaying = false;
        size_t _currentFrame = 0;

        // Callback function for the animation timer
        static void AnimationTimerCallback(void* arg);

};

// Make the static LightDriver instance globally available
extern CLightDriver LightDriver;
