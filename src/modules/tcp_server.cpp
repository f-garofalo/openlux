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
    // Prevent double initialization
    if (server_ != nullptr) {
        LOGI(TAG, "TCP Server already initialized, skipping");
        return;
    }

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

    // Process any pending data from clients (skip marked for removal)
    for (auto& tcp_client : clients_) {
        // Safety check: skip if client is null, disconnected, or pending removal
        if (!tcp_client.client || !tcp_client.client->connected() || tcp_client.pending_removal) {
            continue;
        }

        if (!tcp_client.rx_buffer.empty()) {
            process_client_data(&tcp_client);
        }
    }

    // SAFELY remove clients marked for removal (after all callbacks have returned)
    cleanup_pending_clients();
}

void TCPServer::cleanup_pending_clients() {
    auto it = clients_.begin();
    while (it != clients_.end()) {
        if (it->pending_removal) {
            LOGI(TAG, "Cleaning up client: %s:%d", it->remote_ip.c_str(), it->remote_port);

            // Destroy client using consistent method
            if (it->client) {
                destroy_client(it->client);
            }

            // Clear the vector element
            it->client = nullptr;
            it = clients_.erase(it);


            LOGI(TAG, "Client removed (remaining: %d)", clients_.size());
        } else {
            ++it;
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
        if (tcp_client.client) {
            destroy_client(tcp_client.client);
        }
        tcp_client.client = nullptr;
        tcp_client.rx_buffer.clear();
        tcp_client.rx_buffer.shrink_to_fit();
        tcp_client.remote_ip = "";
    }
    clients_.clear();

    // Stop server
    delete server_;
    server_ = nullptr;

    LOGI(TAG, "TCP Server stopped");
}

void TCPServer::accept_connections() {
    if (!server_) {
        LOGW(TAG, "Cannot accept connections: server not initialized");
        return;
    }

    if (!accepting_connections_) {
        accepting_connections_ = true;
        LOGI(TAG, "✓ Now accepting TCP connections on port %d", port_);
    }
}

void TCPServer::reject_connections() {
    if (!server_) {
        return;
    }

    if (accepting_connections_) {
        accepting_connections_ = false;
        LOGI(TAG, "✗ Now rejecting new TCP connections");

        // Optionally disconnect existing clients
        disconnect_all_clients();
    }
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
    // Reserve more space: ~50 chars per client + header
    out.reserve(64 + clients_.size() * 50);
    out += "Clients: ";
    out += clients_.size(); // Implicit conversion, no temporary String
    out += "\n";
    for (size_t i = 0; i < clients_.size(); i++) {
        const auto& c = clients_[i];
        out += " [";
        out += i;
        out += "] ";
        out += c.remote_ip;
        out += ":";
        out += c.remote_port;
        out += " connected=";
        out += c.is_connected() ? "yes" : "no";
        out += " last_ms=";
        out += c.last_activity;
        out += "\n";
    }
    return out;
}

void TCPServer::disconnect_all_clients() {
    for (auto& tcp_client : clients_) {
        if (tcp_client.client) {
            destroy_client(tcp_client.client);
        }
        tcp_client.client = nullptr;
        tcp_client.rx_buffer.clear();
        tcp_client.rx_buffer.shrink_to_fit();
        tcp_client.remote_ip = "";
    }
    clients_.clear();
}

// ============================================================================
// Callback Handlers
// ============================================================================

void TCPServer::handle_new_client(void* arg, AsyncClient* client) {
    TCPServer* server = static_cast<TCPServer*>(arg);

    // Check if we are accepting connections
    if (!server->accepting_connections_) {
        LOGW(TAG, "Server not ready, rejecting connection from %s:%d",
             client->remoteIP().toString().c_str(), client->remotePort());
        server->destroy_client(client);
        return;
    }

    // Check if we can accept more clients
    if (server->clients_.size() >= server->max_clients_) {
        LOGW(TAG, "Max clients reached, rejecting connection from %s:%d",
             client->remoteIP().toString().c_str(), client->remotePort());
        server->destroy_client(client);
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
        LOGW(TAG, "Received %d bytes from unknown client (already removed)", len);
        return;
    }

    // Skip if client is pending removal or disconnected
    if (tcp_client->pending_removal) {
        LOGW(TAG, "Ignoring %d bytes from client %s pending removal", len,
             tcp_client->remote_ip.c_str());
        return;
    }

    if (!client || !client->connected()) {
        uint8_t* bytes = static_cast<uint8_t*>(data);
        LOGW(TAG, "Received %d bytes from disconnected client %s:", len,
             tcp_client->remote_ip.c_str());
        LOGW(TAG, "   Data: %s", TcpProtocol::format_hex(bytes, len).c_str());
        tcp_client->pending_removal = true; // Mark for cleanup
        return;
    }

    // Append data to buffer efficiently
    uint8_t* bytes = static_cast<uint8_t*>(data);
    const size_t old_size = tcp_client->rx_buffer.size();
    tcp_client->rx_buffer.resize(old_size + len);
    memcpy(&tcp_client->rx_buffer[old_size], bytes, len);
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
    const uint32_t now = millis(); // Single call
    tcp_client.connect_time = now;
    tcp_client.last_activity = now;
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

void TCPServer::remove_client(AsyncClient* client) {
    auto it = std::find_if(clients_.begin(), clients_.end(),
                           [client](const TCPClient& c) { return c.client == client; });

    if (it != clients_.end()) {
        if (!it->pending_removal) {
            LOGI(TAG, "Marking client for removal: %s:%d", it->remote_ip.c_str(), it->remote_port);
            it->pending_removal = true;

            // Clear callbacks immediately to prevent further calls
            if (it->client) {
                it->client->onData(nullptr, nullptr);
                it->client->onError(nullptr, nullptr);
                it->client->onDisconnect(nullptr, nullptr);
                it->client->onTimeout(nullptr, nullptr);
            }
        }
    } else {
        LOGD(TAG, "Client not found in list (already removed or never added)");
    }
}

void TCPServer::destroy_client(AsyncClient* client) {
    if (!client) {
        return;
    }

    // Clear all callbacks first to prevent them from being called during cleanup
    client->onData(nullptr, nullptr);
    client->onError(nullptr, nullptr);
    client->onDisconnect(nullptr, nullptr);
    client->onTimeout(nullptr, nullptr);

    if (client->connected()) {
        client->close();
    }

    client->free();

    delete client; // NOLINT(cppcoreguidelines-owning-memory) - AsyncClient requires manual cleanup
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
    // (Bridge will acquire TCP_CLIENT_PROCESSING guard for coordination)
    bridge_->process_wifi_request(tcp_client->rx_buffer.data(), tcp_client->rx_buffer.size(),
                                  tcp_client);

    // Clear buffer after processing
    tcp_client->rx_buffer.clear();
    LOGD(TAG, "Buffer cleared after processing");
}

void TCPServer::check_client_timeouts() {
    if (clients_.empty()) {
        return;
    }

    const uint32_t now = millis(); // Single call for all clients

    for (auto it = clients_.begin(); it != clients_.end();) {
        // Calculate idle time, handling uint32_t wraparound
        const uint32_t idle_time = now - it->last_activity;

        if (idle_time > CLIENT_TIMEOUT_MS) {
            LOGW(TAG, "Client timeout: %s (idle for %u ms)", it->remote_ip.c_str(), idle_time);

            AsyncClient* c = it->client;

            // Remove from list FIRST to avoid reentrancy issues if close() triggers callbacks
            // immediately
            it = clients_.erase(it);

            // Use consistent destroy method
            destroy_client(c);

            LOGI(TAG, "Client removed due to timeout (remaining: %d)", clients_.size());
        } else {
            ++it;
        }
    }
}
