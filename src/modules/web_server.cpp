/**
 * @file web_server.cpp
 * @brief Web dashboard implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "web_server.h"

#ifdef ENABLE_WEB_DASH

#include "command_manager.h"
#include "logger.h"
#include "network_manager.h"
#include "rs485_manager.h"

#include <Esp.h>

static const char* TAG = "web";

namespace {
const char* kRootPage = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <title>OpenLux Dashboard</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 16px; }
    pre { background: #f5f5f5; padding: 8px; }
    .card { border: 1px solid #ddd; padding: 12px; margin-bottom: 12px; border-radius: 6px; }
    button { padding: 6px 12px; }
    input { padding: 6px; }
    .footer { margin-top: 20px; font-size: 0.85em; color: #888; text-align: center; border-top: 1px solid #eee; padding-top: 10px; }
    .footer a { color: #555; text-decoration: none; }
    .footer a:hover { text-decoration: underline; }
  </style>
</head>
<body>
  <h2>OpenLux Dashboard</h2>
  <div class="card">
    <button onclick="refresh()">Refresh Status</button>
    <pre id="status">Loading...</pre>
  </div>
  <div class="card">
    <div>
      <input id="cmd" type="text" placeholder="command (e.g., help, status, reboot)" size="30" onkeydown="if(event.key==='Enter'){sendCmd();}" />
      <button onclick="sendCmd()">Run</button>
    </div>
    <pre id="cmdRes"></pre>
  </div>
  <div class="footer">
    <a href="https://github.com/f-garofalo/openlux" target="_blank">OpenLux Repository</a>
    <span id="ver_span"></span>
  </div>
  <script>
    async function refresh() {
      try {
        const res = await fetch('/api/status', {credentials:'include'});
        const txt = await res.text();
        try {
          const obj = JSON.parse(txt);
          document.getElementById('status').textContent = JSON.stringify(obj, null, 2);
          let ver = obj.fw || obj.version || obj.firmware_version || obj.sw_version;
          if (ver) {
            document.getElementById('ver_span').textContent = ' | ' + ver;
          }
        } catch (_) {
          document.getElementById('status').textContent = txt;
        }
      } catch (e) {
        document.getElementById('status').textContent = 'Error: ' + e;
      }
    }
    async function sendCmd() {
      const cmd = document.getElementById('cmd').value.trim();
      if (!cmd) return;
      try {
        const res = await fetch('/api/cmd?cmd=' + encodeURIComponent(cmd), {method:'POST', credentials:'include'});
        const txt = await res.text();
        try {
          const obj = JSON.parse(txt);
          if (obj.message !== undefined) {
            document.getElementById('cmdRes').innerHTML = String(obj.message).replace(/\\n/g, '<br/>');
          } else {
            document.getElementById('cmdRes').textContent = JSON.stringify(obj, null, 2);
          }
        } catch (_) {
          document.getElementById('cmdRes').textContent = txt;
        }
      } catch (e) {
        document.getElementById('cmdRes').textContent = 'Error: ' + e;
      }
    }
    refresh();
  </script>
</body>
</html>
)HTML";
} // namespace

WebServerManager& WebServerManager::getInstance() {
    static WebServerManager instance;
    return instance;
}

WebServerManager::WebServerManager() : server_(WEB_DASH_PORT) {}

void WebServerManager::begin() {
    LOGI(TAG, "Starting web dashboard on port %d", WEB_DASH_PORT);
    registerRoutes();
    server_.begin();
}

void WebServerManager::loop() {
    server_.handleClient();
}

bool WebServerManager::requireAuth() {
    if (WEB_DASH_USER[0] == '\0')
        return true; // auth disabled
    if (!server_.authenticate(WEB_DASH_USER, WEB_DASH_PASS)) {
        server_.requestAuthentication();
        return false;
    }
    return true;
}

void WebServerManager::serveRoot() {
    if (!requireAuth())
        return;
    server_.send(200, "text/html", kRootPage);
}

void WebServerManager::handleStatus() {
    if (!requireAuth())
        return;

    // Reuse the existing status command to keep output in sync
    auto& cm = CommandManager::getInstance();
    auto res = cm.execute("!status");

    if (!res.ok) {
        String err = String(R"({"ok":false,"message":")") + res.message + "\"}";
        err.replace("\"", "\\\"");
        err.replace("\n", "\\n");
        server_.send(400, "application/json", err);
        return;
    }

    // Build a simple JSON object from the status lines
    String msg = res.message;
    String json = "{";
    String raw = msg;
    raw.replace("\"", "\\\"");
    raw.replace("\n", "\\n");
    json += R"("raw":")" + raw + "\"";

    int start = 0;
    while (start <= msg.length()) {
        int nl = msg.indexOf('\n', start);
        String line = (nl == -1) ? msg.substring(start) : msg.substring(start, nl);
        line.trim();
        int colon = line.indexOf(':');
        if (colon > 0) {
            String key = line.substring(0, colon);
            String val = line.substring(colon + 1);
            key.trim();
            val.trim();
            key.toLowerCase();
            key.replace(' ', '_');
            key.replace('/', '_');
            String escVal = val;
            escVal.replace("\"", "\\\"");
            escVal.replace("\n", "\\n");
            json += ",\"" + key + "\":\"" + escVal + "\"";
        }
        if (nl == -1)
            break;
        start = nl + 1;
    }

    json += "}";
    server_.send(200, "application/json", json);
}

void WebServerManager::handleCommand() {
    if (!requireAuth())
        return;

    String cmd;
    if (server_.hasArg("cmd")) {
        cmd = server_.arg("cmd");
    } else {
        server_.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing cmd\"}");
        return;
    }
    cmd.trim();
    if (!cmd.startsWith("!")) {
        cmd = "!" + cmd; // align with telnet syntax
    }

    auto& cm = CommandManager::getInstance();
    auto res = cm.execute(cmd);

    String out = "{";
    out += "\"ok\":";
    out += res.ok ? "true" : "false";
    out += ",\"message\":\"";
    String escaped = res.message;
    escaped.replace("\"", "\\\"");
    escaped.replace("\n", "\\n");
    out += escaped;
    out += "\"}";
    server_.send(res.ok ? 200 : 400, "application/json", out);
}

void WebServerManager::registerRoutes() {
    server_.on("/", HTTP_GET, [this]() { serveRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/api/cmd", HTTP_POST, [this]() { handleCommand(); });
}

#endif // ENABLE_WEB_DASH
