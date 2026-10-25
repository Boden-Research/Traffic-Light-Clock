#include <cmath>
#include <climits>
#include <array>
#include <vector>
#include <memory>
#include <utility>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_rom_sys.h>

#include "LightDriver.hpp"

#define SPI_CLOCK_SPEED_HZ     1e6 // 1MHz

// Singleton instance of CLightDriver class, available everywhere
CLightDriver LightDriver;

Result_t CLightDriver::Initialize(int dataPin, int clkPin, int loadPin, int numModules)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing light driver...");
    ESP_LOGD(TAG, "Parameters: DataPin=%d, ClkPin=%d, LoadPin=%d, NumModules=%d",
        dataPin, clkPin, loadPin, numModules);

    // Save parameters we need later
    _dataPin = (gpio_num_t)dataPin;
    _clkPin = (gpio_num_t)clkPin;
    _loadPin = (gpio_num_t)loadPin;
    _numModules = numModules;

    // Set up the SPI bus
    spi_bus_config_t busConfig = {};
    busConfig.miso_io_num = -1; // Not used
    busConfig.mosi_io_num = dataPin;
    busConfig.sclk_io_num = clkPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = numModules;
    err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return Result_t::InitLightFailed;
    }

    // Configure and add the SPI device
    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = SPI_CLOCK_SPEED_HZ;
    deviceConfig.mode = 0; // CPOL=0, CPHA=0: Clock idle low, active high; data clocked in on low-to-high transition
    deviceConfig.spics_io_num = (int)_loadPin;
    deviceConfig.queue_size = 1;
    deviceConfig.flags = SPI_DEVICE_NO_DUMMY; // Not reading anything back, so don't care about inserting dummy bits
    err = spi_bus_add_device(SPI2_HOST, &deviceConfig, &_spiHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device");
        return Result_t::InitLightFailed;
    }

    // Create a mutex for protecting animation state
    _animationMutex = xSemaphoreCreateMutex();
    if (_animationMutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create animation mutex");
        return Result_t::InitLightFailed;
    }

    // Create the animation timer
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &CLightDriver::AnimationTimerCallback;
    timerArgs.arg = this;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "LightAnim";
    err = esp_timer_create(&timerArgs, &_animationTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create animation timer");
        return Result_t::InitLightFailed;
    }

    ESP_LOGI(TAG, "Light driver initialized successfully");

    return Result_t::OK;
}

Result_t CLightDriver::Update(const LightState_t& state) {
    // Reverse the buffer to that the last byte in the buffer is sent first
    // Because of the cascading nature of the shift registers, the last module in the chain needs to receive its data first
    LightState_t reversedState = state;
    std::reverse(reversedState.begin(), reversedState.end());

    // Prepare the SPI transaction structure
    spi_transaction_t transaction = {};
    transaction.length = _numModules * 8; // Length in bits
    transaction.tx_buffer = reversedState.data();

    // Transmit the buffer over SPI
    return EspErrToResult(spi_device_transmit(_spiHandle, &transaction));
}

Result_t CLightDriver::Clear() {
    LightState_t state {};

    return Update(state);
}

Result_t CLightDriver::StartAnimation(LightAnimationPtr animation) {
    // Sanity checks, round 1
    if (!animation) {
        ESP_LOGE(TAG, "Invalid animation pointer");
        return Result_t::InvalidArg;
    }

    // Take ownership of the animation object, even if we discard it later
    LightAnimationPtr tempAnimation = std::move(animation);

    // Sanity checks, round 2
    if (!std::isfinite(tempAnimation->FrameRate) ||
        tempAnimation->FrameRate <= 0.0f ||
        tempAnimation->Frames.empty() ||
        _animationTimer == nullptr ||
        _animationMutex == nullptr) {
            ESP_LOGE(TAG, "Invalid animation state or uninitialized resources");
            return Result_t::InvalidArg;
        }

    ESP_LOGD(TAG, "Starting new animation: FrameRate=%.2f FPS, NumFrames=%zu", tempAnimation->FrameRate, tempAnimation->Frames.size());

    // Stop the previous schedule before replacing its state
    esp_err_t err = esp_timer_stop(_animationTimer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to stop previous animation timer");
        return Result_t::Failed;
    }

    // Acquire the animation mutex to ensure exclusive access to the animation state
    xSemaphoreTake(_animationMutex, portMAX_DELAY);
    
    // Store the animation and mark it as playing
    _animation = std::move(tempAnimation);
    _animationStartUs = esp_timer_get_time();
    _currentFrame = 0;
    _animationPlaying = true;

    // Immediately display the first frame of the new animation
    // Remember the result for later checking outside the critical section
    Result_t result = Update(_animation->Frames[0]);

    // Release the animation mutex after updating the state
    xSemaphoreGive(_animationMutex);

    // Check if updating the first frame succeeded
    if (result != Result_t::OK) {
        // Failed to update the initial frame
        ESP_LOGE(TAG, "Failed to start animation: initial frame update failed");

        // No animation currently playing
        xSemaphoreTake(_animationMutex, portMAX_DELAY);
        _animationPlaying = false;
        xSemaphoreGive(_animationMutex);

        // Return failure reason to the caller
        return result;
    }

    // Periodic callback interval, in microseconds
    const uint64_t periodUs = static_cast<uint64_t>(1.0e6 / _animation->FrameRate); // 1MHz tick rate

    // Start the periodic timer for the animation
    err = esp_timer_start_periodic(_animationTimer, periodUs);
    if (err != ESP_OK) {
        // Failed to start the periodic timer
        ESP_LOGE(TAG, "Failed to start animation: timer start failed");

        // Mark the animation as not playing before returning
        xSemaphoreTake(_animationMutex, portMAX_DELAY);
        _animationPlaying = false;
        xSemaphoreGive(_animationMutex);

        // Return failure reason to the caller
        return Result_t::InitLightFailed;
    }

    ESP_LOGD(TAG, "Animation started successfully: FrameRate=%.2f FPS, NumFrames=%zu", _animation->FrameRate, _animation->Frames.size());

    // Success
    return Result_t::OK;
}

void CLightDriver::AnimationTimerCallback(void* arg)
{
    CLightDriver* self = static_cast<CLightDriver*>(arg);

    // Acquire the animation mutex to safely check the animation state
    xSemaphoreTake(self->_animationMutex, portMAX_DELAY);

    if (!self->_animationPlaying) {
        // Nothing to do
        xSemaphoreGive(self->_animationMutex);
        esp_timer_stop(self->_animationTimer);
        return;
    }

    // Calculate the elapsed time since the animation started, in microseconds
    const int64_t elapsedUs = esp_timer_get_time() - self->_animationStartUs;

    // Determine the current frame index based on the elapsed time and frame rate
    const size_t frameIndex = static_cast<size_t>(elapsedUs * self->_animation->FrameRate / 1000000.0);

    // If the frame index exceeds the number of frames, stop the animation
    if (frameIndex >= self->_animation->Frames.size()) {
        // Animation has finished, stop it
        self->_animationPlaying = false;
        xSemaphoreGive(self->_animationMutex);

        esp_timer_stop(self->_animationTimer);

        return;
    }

    if (frameIndex != self->_currentFrame) {
        // Update the light to the current frame
        Result_t result = self->Update(self->_animation->Frames[frameIndex]);

        if (result == Result_t::OK)
            // Update the current frame index only if the update was successful
            self->_currentFrame = frameIndex;
        else {
            // Failed to update the light, stop the animation
            self->_animationPlaying = false;
            xSemaphoreGive(self->_animationMutex);
            esp_timer_stop(self->_animationTimer);
            return;
        }
    }

    // Release the animation mutex after processing the current frame
    xSemaphoreGive(self->_animationMutex);
}
