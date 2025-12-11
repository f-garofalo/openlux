/**
 * @file tcp_server.h
 * @brief TCP server for Home Assistant connections (port 8000)
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include <AsyncTCP.h>

/**
 * @brief TCP Server for Home Assistant communication
 *
 * Accepts multiple client connections on port 8000 (Luxpower TCP dongle protocol)
 */

// Forward declaration
class ProtocolBridge;

// Client connection structure
struct TCPClient {
    AsyncClient* client = nullptr;
    uint32_t connect_time = 0;
    String remote_ip;
    uint16_t remote_port = 0;
    std::vector<uint8_t> rx_buffer;
    uint32_t last_activity = 0;

    bool is_connected() const { return client != nullptr && client->connected(); }
};

/**
 * @brief TCP Server Manager
 *
 * Handles multiple TCP client connections for Home Assistant integration
 */
class TCPServer {
  public:
    static TCPServer& getInstance();

    // Lifecycle
    void begin(uint16_t port, size_t max_clients = 5);
    void loop();
    void stop();

    // Configuration
    void set_bridge(ProtocolBridge* bridge) { bridge_ = bridge; }

    // Status
    bool is_running() const { return server_ != nullptr; }
    size_t get_client_count() const { return clients_.size(); }
    uint16_t get_port() const { return port_; }

    // Send data to specific client
    bool send_to_client(size_t client_id, const uint8_t* data, size_t length);
    bool send_to_all_clients(const uint8_t* data, size_t length);

    // Statistics
    uint32_t get_total_connections() const { return total_connections_; }
    uint32_t get_total_bytes_rx() const { return total_bytes_rx_; }
    uint32_t get_total_bytes_tx() const { return total_bytes_tx_; }

    // Admin helpers
    String describe_clients() const;
    void disconnect_all_clients();

  private:
    TCPServer() = default;
    ~TCPServer();
    TCPServer(const TCPServer&) = delete;
    TCPServer& operator=(const TCPServer&) = delete;

    // Callback handlers
    static void handle_new_client(void* arg, AsyncClient* client);
    static void handle_client_data(void* arg, AsyncClient* client, void* data, size_t len);
    static void handle_client_disconnect(void* arg, AsyncClient* client);
    static void handle_client_error(void* arg, AsyncClient* client, int8_t error);
    static void handle_client_timeout(void* arg, AsyncClient* client, uint32_t time);

    // Internal methods
    void add_client(AsyncClient* client);
    void remove_client(AsyncClient* client);
    TCPClient* find_client(AsyncClient* client);
    void process_client_data(TCPClient* tcp_client);
    void check_client_timeouts();

    AsyncServer* server_ = nullptr;
    std::vector<TCPClient> clients_;
    size_t max_clients_ = 5;
    uint16_t port_ = 8000;
    ProtocolBridge* bridge_ = nullptr;

    // Statistics
    uint32_t total_connections_ = 0;
    uint32_t total_bytes_rx_ = 0;
    uint32_t total_bytes_tx_ = 0;

    // Timeout settings
    static constexpr uint32_t CLIENT_TIMEOUT_MS = 300000;  // 5 minutes (era 60 sec)
    static constexpr uint32_t INTER_PACKET_DELAY_MS = 100; // 100ms between packets
};
