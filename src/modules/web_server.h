/**
 * @file web_server.h
 * @brief Web dashboard and API server
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include "../config.h"

#ifdef ENABLE_WEB_DASH

#include <Arduino.h>

#include <WebServer.h>

class WebServerManager {
  public:
    static WebServerManager& getInstance();

    void begin();
    void loop();

  private:
    WebServerManager();
    ~WebServerManager() = default;
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;

    void registerRoutes();
    bool requireAuth();
    void handleStatus();
    void handleCommand();
    void serveRoot();

    WebServer server_;
};

#endif // ENABLE_WEB_DASH
