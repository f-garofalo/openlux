/**
 * @file mqtt_manager.h
 * @brief MQTT Client Manager
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include "../config.h"

#ifdef ENABLE_MQTT

#include <Arduino.h>

#include <PubSubClient.h>
#if OPENLUX_USE_ETHERNET
#include <Ethernet.h>
#else
#include <WiFiClient.h>
#endif

class MqttManager {
  public:
    static MqttManager& getInstance();

    void begin();
    void loop();

    bool isConnected();
    void publish(const char* topic, const char* payload, bool retained = false);
    void publishStatus();
    void publishDiscovery();

  private:
    MqttManager();
    ~MqttManager() = default;
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;

    void connect();
    void onMessage(char* topic, uint8_t* payload, unsigned int length);
    void subscribeTopics();

#if OPENLUX_USE_ETHERNET
    EthernetClient net_client_; // For Ethernet
#else
    WiFiClient net_client_; // For WiFi
#endif
    PubSubClient mqtt_client_;

    uint32_t last_reconnect_attempt_ = 0;
    uint32_t last_status_publish_ = 0;
    uint32_t consecutive_failures_ = 0;
    bool configured_ = false;

    String base_topic_;
    String status_topic_;
    String command_topic_;
    String availability_topic_;
};

#endif // ENABLE_MQTT
