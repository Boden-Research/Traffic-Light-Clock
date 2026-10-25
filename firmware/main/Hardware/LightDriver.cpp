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

#define CLOCK_DELAY_US 10

// Singleton instance of CLightDriver class, available everywhere
CLightDriver LightDriver;

static void LogLightState(LightState_t& state) {
    ESP_LOG_BUFFER_HEX_LEVEL(CLightDriver::TAG, state.data(), 11, ESP_LOG_DEBUG);
}

static void SendBit(bool bit, gpio_num_t dataPin, gpio_num_t clockPin) {
    gpio_set_level(dataPin, bit ? 1 : 0);
    esp_rom_delay_us(CLOCK_DELAY_US);
    gpio_set_level(clockPin, 1);
    esp_rom_delay_us(CLOCK_DELAY_US);
    gpio_set_level(clockPin, 0);
    esp_rom_delay_us(CLOCK_DELAY_US);
}

static void SendByte(uint8_t byte, gpio_num_t dataPin, gpio_num_t clockPin) {
    for (uint8_t bit = 0; bit < 7; bit++) {
        SendBit(byte & 0x01, dataPin, clockPin);
        byte >>= 1;
    }
}

static void SendLoad(gpio_num_t loadPin) {
    gpio_set_level(loadPin, 1);
    esp_rom_delay_us(CLOCK_DELAY_US);
    gpio_set_level(loadPin, 0);
}

static void SendBuffer(uint8_t* buffer, size_t bufferLen, gpio_num_t dataPin, gpio_num_t clockPin, gpio_num_t loadPin) {
    while (bufferLen > 0) {
        SendByte(*buffer++, dataPin, clockPin);
        bufferLen--;
    }
    SendLoad(loadPin);
}

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

    // Set up the LOAD pin
    gpio_config_t gpioConfig = {};
    gpioConfig.pin_bit_mask = (1ULL << _dataPin) | (1ULL << _clkPin) | (1ULL << _loadPin);
    gpioConfig.mode = GPIO_MODE_OUTPUT;
    gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpioConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpioConfig.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&gpioConfig);
    if (err != ESP_OK)
        return Result_t::InitLightFailed;
    gpio_set_level(_dataPin, 0);
    gpio_set_level(_clkPin, 0);
    gpio_set_level(_loadPin, 0);

/*
    // Set up the SPI bus
    spi_bus_config_t busConfig = {};
    busConfig.miso_io_num = -1; // Not used
    busConfig.mosi_io_num = dataPin;
    busConfig.sclk_io_num = clkPin;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = numModules;
    err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
        return Result_t::InitLightFailed;

    // Configure and add the SPI device
    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = 1e3; // 1e6 == 1,000,000 == 1MHz
    deviceConfig.mode = 0; // CPOL=0, CPHA=0: Clock idle low, active high; data clocked in on low-to-high transition
    deviceConfig.spics_io_num = -1;
    deviceConfig.queue_size = 1;
    deviceConfig.flags = SPI_DEVICE_NO_DUMMY; // Not reading anything back, so don't care about inserting dummy bits
    err = spi_bus_add_device(SPI2_HOST, &deviceConfig, &_spiHandle);
    if (err != ESP_OK)
        return Result_t::InitLightFailed;
*/

    ESP_LOGI(TAG, "Light driver initialized successfully");

    return Result_t::OK;
}

Result_t CLightDriver::Update(LightState_t& state) {
//    Result_t result;

    LogLightState(state);

    SendBuffer(state.data(), _numModules, _dataPin, _clkPin, _loadPin);
    SendBuffer(state.data(), _numModules, _dataPin, _clkPin, _loadPin);
    SendBuffer(state.data(), _numModules, _dataPin, _clkPin, _loadPin);

/*
    // Set up the transaction
    spi_transaction_t tx = {};
    tx.length = _numModules;
    tx.tx_buffer = state.data();

    // Transmit the new state and return result
    result = EspErrToResult(spi_device_transmit(_spiHandle, &tx));
    if (result != Result_t::OK)
        return result;


    // Pulse the LOAD pin
    // TODO: Is there a cleaner way to do this?
    gpio_set_level(_loadPin, 1);
    vTaskDelay(1);
    gpio_set_level(_loadPin, 0);
*/

    return Result_t::OK;
}

Result_t CLightDriver::Clear() {
//    esp_err_t err;

    // Create buffer for holding output values
    uint8_t* buff = (uint8_t*)malloc(_numModules);
    if (!buff)
        return Result_t::OutOfMemory; // Out of memory

    // Zero out the buffer
    memset(buff, 0, _numModules);

    SendBuffer(buff, _numModules, _dataPin, _clkPin, _loadPin);
/*
    // Set up a transaction to send new state
    spi_transaction_t tx = {};
    tx.length = _numModules;
    tx.tx_buffer = buff;

    // Transmit the new state and return result
    err = spi_device_transmit(_spiHandle, &tx);
*/

    // Clean up buffer
    // Do this whether transmission succeeds or fails
    free(buff);
    buff = nullptr;

    // Return result
//    return EspErrToResult(err);
    return Result_t::OK;
}