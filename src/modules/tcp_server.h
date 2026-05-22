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
 * Accepts multiple client connections on port 8000 (TCP dongle protocol)
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
    bool pending_removal = false; // Mark for safe removal during loop

    bool is_connected() const {
        return client != nullptr && client->connected() && !pending_removal;
    }
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
    bool is_accepting_connections() const { return accepting_connections_; }
    uint32_t get_listener_restart_count() const { return listener_restart_count_; }
    uint32_t get_listener_health_failures() const { return listener_health_failures_; }
    uint32_t get_listener_health_checks() const { return listener_health_checks_; }
    uint32_t get_listener_health_successes() const { return listener_health_successes_; }

    // Connection control
    void accept_connections();
    void reject_connections();

    // Send data to specific client
    bool send_to_client(size_t client_id, const uint8_t* data, size_t length);
    bool send_to_all_clients(const uint8_t* data, size_t length);

    // Look up a live TCPClient by its AsyncClient handle. Returns nullptr if
    // the client has disconnected or been removed. The AsyncClient* is a
    // stable heap handle, so it's safe to keep across loop iterations — but
    // the TCPClient entry itself lives in a std::vector that can move, so
    // always resolve through this method immediately before use.
    TCPClient* resolve_client(const AsyncClient* async_client);

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
    void cleanup_pending_clients();
    TCPClient* find_client(const AsyncClient* client);
    void process_client_data(TCPClient* tcp_client);
    void check_client_timeouts();
    void check_listener_health();
    bool run_listener_self_probe();
    bool is_self_probe_client(const AsyncClient* client) const;
    void restart_listener(const char* reason);
    void destroy_client(AsyncClient* client);

    AsyncServer* server_ = nullptr;
    std::vector<TCPClient> clients_;
    size_t max_clients_ = 5;
    uint16_t port_ = 8000;
    ProtocolBridge* bridge_ = nullptr;
    bool accepting_connections_ = false;

    // Statistics
    uint32_t total_connections_ = 0;
    uint32_t total_bytes_rx_ = 0;
    uint32_t total_bytes_tx_ = 0;
    uint32_t listener_restart_count_ = 0;
    uint32_t listener_health_checks_ = 0;
    uint32_t listener_health_successes_ = 0;
    uint32_t listener_health_failures_ = 0;
    uint32_t last_listener_health_check_ms_ = 0;
    uint32_t last_listener_restart_ms_ = 0;

    // Timeout settings
    static constexpr uint32_t CLIENT_TIMEOUT_MS = 300000;  // 5 minutes idle before disconnect
    static constexpr uint32_t INTER_PACKET_DELAY_MS = 100; // 100ms between packets
    static constexpr uint32_t LISTENER_HEALTH_INTERVAL_MS = 15000;
    static constexpr uint32_t LISTENER_HEALTH_CONNECT_TIMEOUT_MS = 350;
    static constexpr uint32_t LISTENER_RESTART_COOLDOWN_MS = 90000;
    static constexpr uint8_t LISTENER_HEALTH_MAX_FAILURES = 3;

    // Max per-client RX buffer. A well-formed request is 38 bytes; a write_multi
    // request with 127 registers tops out under 300 bytes. 4 KiB is ample for
    // stacked requests yet protects against heap exhaustion from a slow-drip
    // attacker sending unbounded bytes without a parseable frame.
    static constexpr size_t MAX_RX_BUFFER_SIZE = 4096;
};
