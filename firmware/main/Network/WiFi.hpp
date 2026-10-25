#pragma once

#include <inttypes.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif_types.h>

#include "../EventBus/EventBus.hpp"
#include "../DataTypes.hpp"

/// @brief singleton class for providing network interaction and notifications.
class CWiFi {
    public:
        // Log tag
        static constexpr const char* TAG = "CWiFi";

        /// @brief Initializes Wi-Fi driver and network stack.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t Initialize();

        /// @brief Sets up a new Wi-Fi access point and DHCP server for clients to connect to.
        /// @param ssid The SSID of the network to create.
        /// @param pass The password clients must use to connect to this access point.
        /// @param channel The Wi-Fi channel to run the access point on.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t StartAP(const char* ssid, const char* pass, uint8_t channel);

        /// @brief Shuts down any runing Wi-Fi access point and DHCP server.
        void StopAP();

        /// @brief Connects to an existing Wi-Fi access point as a client.
        /// @param ssid The SSID of the network to connect to.
        /// @param pass The password to use to connect to the network.
        /// @return ESP_OK on success, otherwise returns an error code.
        Result_t ConnectSTA(const char* ssid, const char* pass);

        /// @brief Disconnects from any connected Wi-Fi access point.
        void DisconnectSTA();

        /// @brief Fires when a Wi-Fi station connection succeeds and IP address is obtained. Event data is our IP/mask/gateway.
        EventBus<esp_netif_ip_info_t> WiFiStationConnectedEvent;

        /// @brief Fires when a Wi-Fi station connection drops. May be dropped by the network or dropped by calling DisconnectSTA().
        EventBus<void> WiFiStationDisconnectedEvent;

        /// @brief Fires when a Wi-Fi access point has been configured and activated. Event data is our client IP address.
        EventBus<esp_netif_ip_info_t> WiFiAccessPointActiveEvent;

        /// @brief Fires when a station connects to our Wi-fi access point and obtains an IP address. Event data is the MAC address of the station.
        EventBus<uint8_t[6]> WiFiAccessPointStationConnectedEvent;

        /// @brief Fires when a station disconnects from our Wi-fi access point. Event data is the MAC address of the station.
        EventBus<uint8_t[6]> WiFiAccessPointStationDisconnectedEvent;

        /// @brief fires when the Wi-Fi access point is shut down.
        EventBus<void> WiFiAccessPointDeactivatedEvent;

    private:
        // Tracks if Wi-Fi has already been initialized
        bool _wifiInitialized = false;

        // Instance variables for access point and station modes
        esp_netif_t* _netifAP;
        esp_netif_t* _netifSTA;

        esp_event_handler_instance_t _eventAnyId; // Event handler instance for Wi-Fi connect/reconnect/disconnect events
        esp_event_handler_instance_t _eventGotIp; // Event handler for DHCP events
        static void WiFiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

};

// Make the static WiFi instance globally available
extern CWiFi WiFi;
