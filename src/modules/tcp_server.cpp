/**
 * @file tcp_server.cpp
 * @brief TCP server implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "tcp_server.h"

#include "../config.h"
#include "logger.h"
#include "protocol_bridge.h"
#include "tcp_protocol.h"

#include <algorithm>

static const char* TAG = "tcp";

TCPServer& TCPServer::getInstance() {
    static TCPServer instance;
    return instance;
}

TCPServer::~TCPServer() {
    stop();
}

void TCPServer::begin(uint16_t port, size_t max_clients) {
    port_ = port;
    max_clients_ = max_clients;

    LOGI(TAG, "Starting TCP Server on port %d", port_);
    LOGI(TAG, "  Max clients: %d", max_clients_);

    server_ = new AsyncServer(port_);

    // Set up callback for new connections
    server_->onClient(
        [](void* arg, AsyncClient* client) { TCPServer::handle_new_client(arg, client); }, this);

    server_->begin();

    LOGI(TAG, "TCP Server started successfully");
}

void TCPServer::loop() {
    if (!server_) {
        return;
    }

    // Check for client timeouts (FIRST, before processing data)
    check_client_timeouts();

    // Process any pending data from clients (AFTER timeout check)
    for (auto& tcp_client : clients_) {
        // Safety check: skip if client is null or disconnected
        if (!tcp_client.client || !tcp_client.client->connected()) {
            LOGD(TAG, "Skipping disconnected client in loop");
            continue;
        }

        if (!tcp_client.rx_buffer.empty()) {
            process_client_data(&tcp_client);
        }
    }
}

void TCPServer::stop() {
    if (!server_) {
        return;
    }

    LOGI(TAG, "Stopping TCP Server...");

    // Disconnect all clients safely
    for (auto& tcp_client : clients_) {
        if (tcp_client.client && tcp_client.client->connected()) {
            tcp_client.client->close(true);
        }
        // Don't delete - AsyncTCP handles it
        tcp_client.client = nullptr;
    }
    clients_.clear();

    // Stop server
    delete server_;
    server_ = nullptr;

    LOGI(TAG, "TCP Server stopped");
}

bool TCPServer::send_to_client(size_t client_id, const uint8_t* data, size_t length) {
    if (client_id >= clients_.size() || !clients_[client_id].is_connected()) {
        return false;
    }

    size_t written = clients_[client_id].client->write(reinterpret_cast<const char*>(data), length);
    if (written == length) {
        total_bytes_tx_ += length;
        clients_[client_id].last_activity = millis();
        return true;
    }

    return false;
}

bool TCPServer::send_to_all_clients(const uint8_t* data, size_t length) {
    bool success = false;
    for (size_t i = 0; i < clients_.size(); i++) {
        if (send_to_client(i, data, length)) {
            success = true;
        }
    }
    return success;
}

String TCPServer::describe_clients() const {
    String out;
    out.reserve(128);
    out += "Clients: ";
    out += String(clients_.size());
    out += "\n";
    for (size_t i = 0; i < clients_.size(); i++) {
        const auto& c = clients_[i];
        out += " [" + String(i) + "] ";
        out += c.remote_ip;
        out += ":";
        out += String(c.remote_port);
        out += " connected=";
        out += c.is_connected() ? "yes" : "no";
        out += " last_ms=";
        out += String(c.last_activity);
        out += "\n";
    }
    return out;
}

void TCPServer::disconnect_all_clients() {
    for (auto& tcp_client : clients_) {
        if (tcp_client.client && tcp_client.client->connected()) {
            tcp_client.client->close(true);
        }
        tcp_client.client = nullptr;
    }
    clients_.clear();
}

// ============================================================================
// Callback Handlers
// ============================================================================

void TCPServer::handle_new_client(void* arg, AsyncClient* client) {
    TCPServer* server = static_cast<TCPServer*>(arg);

    // Check if we can accept more clients
    if (server->clients_.size() >= server->max_clients_) {
        LOGW(TAG, "Max clients reached, rejecting connection from %s:%d",
             client->remoteIP().toString().c_str(), client->remotePort());
        client->close(true);
        delete client;
        return;
    }

    LOGI(TAG, "✓ New client connected from %s:%d", client->remoteIP().toString().c_str(),
         client->remotePort());

    server->add_client(client);
}

void TCPServer::handle_client_data(void* arg, AsyncClient* client, void* data, size_t len) {
    TCPServer* server = static_cast<TCPServer*>(arg);
    TCPClient* tcp_client = server->find_client(client);

    if (!tcp_client) {
        LOGW(TAG, "Received data from unknown client (might be during cleanup)");
        return;
    }

    if (!client || !client->connected()) {
        LOGW(TAG, "Received data from disconnected client");
        return;
    }

    // Append data to buffer
    uint8_t* bytes = static_cast<uint8_t*>(data);
    tcp_client->rx_buffer.insert(tcp_client->rx_buffer.end(), bytes, bytes + len);
    tcp_client->last_activity = millis();
    server->total_bytes_rx_ += len;

    LOGD(TAG, "RX from %s: %d bytes (buffer total: %d)", tcp_client->remote_ip.c_str(), len,
         tcp_client->rx_buffer.size());
}

void TCPServer::handle_client_disconnect(void* arg, AsyncClient* client) {
    TCPServer* server = static_cast<TCPServer*>(arg);
    server->remove_client(client);
}

void TCPServer::handle_client_error(void* arg, AsyncClient* client, int8_t error) {
    TCPServer* server = static_cast<TCPServer*>(arg);
    // Log error here as we have the error code, but rely on remove_client for IP logging
    // Note: client->remoteIP() might be invalid on error too
    server->remove_client(client);
}

void TCPServer::handle_client_timeout(void* arg, AsyncClient* client, uint32_t time) {
    TCPServer* server = static_cast<TCPServer*>(arg);
    server->remove_client(client);
}

// ============================================================================
// Internal Methods
// ============================================================================

void TCPServer::add_client(AsyncClient* client) {
    TCPClient tcp_client;
    tcp_client.client = client;
    tcp_client.connect_time = millis();
    tcp_client.last_activity = millis();
    tcp_client.remote_ip = client->remoteIP().toString();
    tcp_client.remote_port = client->remotePort();

    // Set up callbacks
    client->onData([](void* arg, AsyncClient* c, void* data,
                      size_t len) { TCPServer::handle_client_data(arg, c, data, len); },
                   this);

    client->onDisconnect(
        [](void* arg, AsyncClient* c) { TCPServer::handle_client_disconnect(arg, c); }, this);

    client->onError([](void* arg, AsyncClient* c,
                       int8_t error) { TCPServer::handle_client_error(arg, c, error); },
                    this);

    client->onTimeout([](void* arg, AsyncClient* c,
                         uint32_t time) { TCPServer::handle_client_timeout(arg, c, time); },
                      this);

    clients_.push_back(tcp_client);
    total_connections_++;

    LOGI(TAG, "Client added (total: %d/%d)", clients_.size(), max_clients_);
}

void TCPServer::remove_client(const AsyncClient* client) {
    auto it = std::find_if(clients_.begin(), clients_.end(),
                           [client](const TCPClient& c) { return c.client == client; });

    if (it != clients_.end()) {
        LOGI(TAG, "Client disconnected: %s:%d", it->remote_ip.c_str(), it->remote_port);

        // IMPORTANT: Don't delete client pointer!
        // AsyncTCP manages the lifecycle and will delete it automatically
        // Just remove from our vector
        it->client = nullptr; // Clear pointer before erase
        clients_.erase(it);

        LOGI(TAG, "Client removed (remaining: %d)", clients_.size());
    } else {
        // This is normal if we already removed it (e.g. in check_client_timeouts)
        LOGD(TAG, "Client disconnected (already removed)");
    }
}

TCPClient* TCPServer::find_client(const AsyncClient* client) {
    // Safety check
    if (!client) {
        return nullptr;
    }

    auto it = std::find_if(clients_.begin(), clients_.end(),
                           [client](const TCPClient& c) { return c.client == client; });

    if (it != clients_.end()) {
        return &(*it);
    }
    return nullptr;
}

void TCPServer::process_client_data(TCPClient* tcp_client) {
    if (!bridge_) {
        LOGW(TAG, "No bridge configured, dropping data");
        tcp_client->rx_buffer.clear();
        return;
    }

    LOGD(TAG, "Processing buffer: %d bytes", tcp_client->rx_buffer.size());

    // Check if we have a complete packet (38 bytes for WiFi request)
    if (tcp_client->rx_buffer.size() < 38) {
        LOGD(TAG, "Waiting for more data (have %d, need 38)", tcp_client->rx_buffer.size());
        return; // Wait for more data
    }

    LOGI(TAG, "→ Forwarding %d bytes to bridge from %s", tcp_client->rx_buffer.size(),
         tcp_client->remote_ip.c_str());

    // Try to parse and forward to bridge
    // The bridge will handle the packet and send response back
    bridge_->process_wifi_request(tcp_client->rx_buffer.data(), tcp_client->rx_buffer.size(),
                                  tcp_client);

    // Clear buffer after processing
    tcp_client->rx_buffer.clear();
    LOGD(TAG, "Buffer cleared after processing");
}

void TCPServer::check_client_timeouts() {
    uint32_t now = millis();

    for (auto it = clients_.begin(); it != clients_.end();) {
        if (now - it->last_activity > CLIENT_TIMEOUT_MS) {
            LOGW(TAG, "Client timeout: %s (idle for %d ms)", it->remote_ip.c_str(),
                 now - it->last_activity);

            AsyncClient* c = it->client;

            // Remove from list FIRST to avoid reentrancy issues if close() triggers callbacks
            // immediately
            it->client = nullptr;
            it = clients_.erase(it);

            // Close the connection to ensure AsyncTCP cleans up resources
            if (c && c->connected()) {
                c->close();
            }

            LOGI(TAG, "Client removed due to timeout (remaining: %d)", clients_.size());
        } else {
            ++it;
        }
    }
}
