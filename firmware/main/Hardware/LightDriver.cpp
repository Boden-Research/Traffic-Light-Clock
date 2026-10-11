#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_err.h>

#include "LightDriver.hpp"

// Singleton instance of CLightDriver class, available everywhere
CLightDriver LightDriver;

esp_err_t CLightDriver::Initialize(int dataPin, int clkPin, int loadPin, int numModules)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing light driver...");

    // Save parameters we need later
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
    if (err != ESP_OK)
        return err;

    // Configure and add the SPI device
    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = 1e6; // 1e6 == 1,000,000 == 1MHz
    deviceConfig.mode = 0; // CPOL=0, CPHA=0: Clock idle low, active high; data clocked in on low-to-high transition
    deviceConfig.spics_io_num = loadPin; // CS is active low, so deselecting the device triggers a data load into output latches
    deviceConfig.queue_size = 1;
    deviceConfig.flags = SPI_DEVICE_NO_DUMMY; // Not reading anything back, so don't care about inserting dummy bits
    err = spi_bus_add_device(SPI2_HOST, &deviceConfig, &_spiHandle);
    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "Light driver initialized successfully");

    return err;
}

esp_err_t CLightDriver::Update(uint8_t* states) {
    // Set up the transaction
    spi_transaction_t tx = {};
    tx.length = _numModules;
    tx.tx_buffer = states;

    // Transmit the new state and return result
    return spi_device_transmit(_spiHandle, &tx);
}

esp_err_t CLightDriver::Clear() {
    esp_err_t err;

    // Create buffer for holding output values
    uint8_t* buff = (uint8_t*)malloc(_numModules);
    if (!buff)
        return ESP_ERR_NO_MEM; // Out of memory

    // Zero out the buffer
    memset(buff, 0, _numModules);

    // Set up a transaction to send new state
    spi_transaction_t tx = {};
    tx.length = _numModules;
    tx.tx_buffer = buff;

    // Transmit the new state and return result
    err = spi_device_transmit(_spiHandle, &tx);

    // Clean up buffer
    // Do this whether transmission succeeds or fails
    free(buff);
    buff = nullptr;

    // Return result
    return err;
}