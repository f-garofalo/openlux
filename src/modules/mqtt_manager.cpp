/**
 * @file mqtt_manager.cpp
 * @brief MQTT Client Manager implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "mqtt_manager.h"

#ifdef ENABLE_MQTT

#include "command_manager.h"
#include "logger.h"
#include "network_manager.h"
#include "rs485_manager.h"
#include "system_manager.h"

static const char* TAG = "mqtt";

MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager() : mqtt_client_(wifi_client_) {}

void MqttManager::begin() {
    if (strlen(MQTT_HOST) == 0) {
        LOGW(TAG, "MQTT Host not configured, disabling MQTT");
        configured_ = false;
        return;
    }

    configured_ = true;
    mqtt_client_.setServer(MQTT_HOST, MQTT_PORT);
    mqtt_client_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        this->onMessage(topic, payload, length);
    });

    base_topic_ = MQTT_TOPIC_PREFIX;
    if (base_topic_.endsWith("/")) {
        base_topic_.remove(base_topic_.length() - 1);
    }

    status_topic_ = base_topic_ + "/status";
    command_topic_ = base_topic_ + "/cmd";
    availability_topic_ = base_topic_ + "/availability";

    LOGI(TAG, "MQTT Initialized (Broker: %s:%d)", MQTT_HOST, MQTT_PORT);
}

void MqttManager::loop() {
    if (!configured_)
        return;

    if (!NetworkManager::getInstance().isConnected()) {
        // Reset reconnect timer when network is down to avoid spam
        if (mqtt_client_.connected()) {
            mqtt_client_.disconnect();
        }
        return;
    }

    if (!mqtt_client_.connected()) {
        const uint32_t now = millis();
        // Increase retry delay during network instability
        uint32_t retry_delay = 5000;
        if (WiFi.status() != WL_CONNECTED) {
            retry_delay = retry_delay * 2;
        }

        // CRITICAL: Exponential backoff after failures to prevent watchdog reset
        // Multiple blocking connect() attempts can exceed 30s watchdog timeout
        if (consecutive_failures_ >= 3) {
            retry_delay = 30000; // 30s after 3+ failures
        } else if (consecutive_failures_ >= 2) {
            retry_delay = 15000; // 15s after 2 failures
        }

        if (now - last_reconnect_attempt_ > retry_delay) {
            last_reconnect_attempt_ = now;
            connect();
        }
    } else {
        mqtt_client_.loop();

        // Periodic status update
        uint32_t now = millis();
        if (now - last_status_publish_ > MQTT_STATUS_INTERVAL_MS) {
            last_status_publish_ = now;
            publishStatus();
        }
    }
}

void MqttManager::connect() {
    LOGI(TAG, "Attempting MQTT connection...");

    String clientId = MQTT_CLIENT_ID;
    // Append last 3 bytes of MAC to make it unique if default
    String mac = NetworkManager::getInstance().getMAC();
    mac.replace(":", "");
    if (clientId == "openlux-bridge") {
        clientId += "-" + mac.substring(6);
    }

    wifi_client_.setTimeout(3);

    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
        connected = mqtt_client_.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                                         availability_topic_.c_str(), 0, true, "offline");
    } else {
        connected = mqtt_client_.connect(clientId.c_str(), nullptr, nullptr,
                                         availability_topic_.c_str(), 0, true, "offline");
    }

    if (connected) {
        LOGI(TAG, "MQTT Connected!");
        mqtt_client_.publish(availability_topic_.c_str(), "online", true);
        subscribeTopics();
        publishDiscovery();
        publishStatus();
        consecutive_failures_ = 0;
    } else {
        consecutive_failures_++;
        int state = mqtt_client_.state();

        // Only log error every 5 failures to reduce spam during network issues
        if (consecutive_failures_ == 1 || consecutive_failures_ % 5 == 0) {
            LOGE(TAG, "MQTT Connect failed, rc=%d (failures: %d)", state, consecutive_failures_);
        }
    }
}

void MqttManager::subscribeTopics() {
    mqtt_client_.subscribe(command_topic_.c_str());
    LOGI(TAG, "Subscribed to %s", command_topic_.c_str());
}

void MqttManager::onMessage(char* topic, uint8_t* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) {
        msg += (char) payload[i];
    }
    LOGI(TAG, "Message arrived [%s]: %s", topic, msg.c_str());

    if (String(topic) == command_topic_) {
        CommandResult res = CommandManager::getInstance().execute(msg);
        String replyTopic = String(topic) + "/result";
        String reply = res.ok ? "OK: " : "ERROR: ";
        reply += res.message;
        publish(replyTopic.c_str(), reply.c_str());
    }
}

bool MqttManager::isConnected() {
    return mqtt_client_.connected();
}

void MqttManager::publish(const char* topic, const char* payload, bool retained) {
    if (mqtt_client_.connected()) {
        if (!mqtt_client_.publish(topic, payload, retained)) {
            LOGE(TAG, "MQTT Publish failed (payload length: %d). Check MQTT_MAX_PACKET_SIZE.",
                 strlen(payload));
        }
    }
}

void MqttManager::publishDiscovery() {
    if (!mqtt_client_.connected())
        return;

    LOGI(TAG, "Publishing Home Assistant discovery configs...");

    const char* disc_prefix = MQTT_DISCOVERY_PREFIX;
    String device_json =
        "\"device\":{\"identifiers\":[\"" + String(MQTT_CLIENT_ID) +
        "\"],\"name\":\"OpenLux Bridge\",\"model\":\"ESP32 Bridge\",\"sw_version\":\"" +
        String(FIRMWARE_VERSION) + "\",\"manufacturer\":\"OpenLux\"}";
    String avail_json = "\"availability_topic\":\"" + availability_topic_ + "\"";

    struct SensorConfig {
        const char* id;
        const char* name;
        const char* dev_class;
        const char* unit;
        const char* val_tpl;
        const char* icon;
        bool is_binary;
    };

    SensorConfig sensors[] = {
        {"rssi", "WiFi Signal", "signal_strength", "dBm", "{{ value_json.rssi }}", "mdi:wifi",
         false},
        {"uptime", "Uptime", "duration", "s", "{{ value_json.uptime }}", "mdi:clock-outline",
         false},
        {"heap", "Free Heap", "data_size", "B", "{{ value_json.heap }}", "mdi:memory", false},
        {"ip", "IP Address", nullptr, nullptr, "{{ value_json.ip }}", "mdi:ip-network", false},
        {"version", "Firmware Version", nullptr, nullptr, "{{ value_json.version }}", "mdi:chip",
         false},
        {"link_up", "Inverter Link", "connectivity", nullptr, "{{ value_json.link_up }}",
         "mdi:serial-port", true}};

    for (const auto& s : sensors) {
        String topic = String(disc_prefix) + (s.is_binary ? "/binary_sensor/" : "/sensor/") +
                       MQTT_CLIENT_ID + "/" + s.id + "/config";

        String payload = "{";
        payload += "\"name\":\"" + String(s.name) + "\",";
        payload += "\"unique_id\":\"" + String(MQTT_CLIENT_ID) + "_" + s.id + "\",";
        payload += "\"state_topic\":\"" + status_topic_ + "\",";
        if (s.val_tpl)
            payload += "\"value_template\":\"" + String(s.val_tpl) + "\",";
        if (s.dev_class)
            payload += "\"device_class\":\"" + String(s.dev_class) + "\",";
        if (s.unit)
            payload += "\"unit_of_measurement\":\"" + String(s.unit) + "\",";
        if (s.icon)
            payload += "\"icon\":\"" + String(s.icon) + "\",";
        if (s.is_binary)
            payload += "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",";
        payload += avail_json + ",";
        payload += device_json;
        payload += "}";

        publish(topic.c_str(), payload.c_str(), true);
    }
}

void MqttManager::publishStatus() {
    if (!mqtt_client_.connected())
        return;

    // Simple JSON construction to avoid extra library dependency for now
    String json = "{";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"rssi\":" + String(NetworkManager::getInstance().getRSSI()) + ",";
    json += "\"ip\":\"" + NetworkManager::getInstance().getIP().toString() + "\",";
    json += "\"link_up\":\"" +
            String(RS485Manager::getInstance().is_inverter_link_up() ? "ON" : "OFF") + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\"";
    json += "}";

    publish(status_topic_.c_str(), json.c_str());
}

#endif // ENABLE_MQTT
