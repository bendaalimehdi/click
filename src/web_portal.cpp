#include "web_portal.h"
#include <ArduinoJson.h>
#include <WiFi.h>

WebPortal::WebPortal(uint16_t port) : _server(port) {}

void WebPortal::begin(const AppConfig& cfg,
                      SaveConfigCallback saveCb,
                      GetNodesCallback nodesCb,
                      GetStatusCallback statusCb) {
    _cfg = cfg;
    _saveCb = saveCb;
    _nodesCb = nodesCb;
    _statusCb = statusCb;
    setupRoutes();
    _server.begin();
}

void WebPortal::setupRoutes() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->send(200, "text/html", htmlPage());
    });

    _server.on("/api/nodes", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        auto nodes = _nodesCb ? _nodesCb() : std::vector<NodeRecord>{};
        for (const auto& n : nodes) {
            JsonObject o = arr.add<JsonObject>();
            o["client"] = n.client;
            o["id"] = n.id;
            o["temp"] = n.temp;
            o["volt"] = n.volt;
            o["last_seen_ms"] = n.lastSeenMs;
            o["mac"] = n.mac;
        }

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        PortalStatus st = _statusCb ? _statusCb() : PortalStatus{};
        JsonDocument doc;
        doc["queue_size"] = st.queueSize;
        doc["last_error"] = st.lastError;
        doc["device_mac"] = WiFi.macAddress();

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* request) {
        AppConfig newCfg = _cfg;

        if (request->hasParam("device_name", true))
            newCfg.device_name = request->getParam("device_name", true)->value();
        if (request->hasParam("wifi_ssid", true))
            newCfg.wifi_ssid = request->getParam("wifi_ssid", true)->value();
        if (request->hasParam("wifi_pass", true))
            newCfg.wifi_pass = request->getParam("wifi_pass", true)->value();
        if (request->hasParam("server_url", true))
            newCfg.server_url = request->getParam("server_url", true)->value();
        if (request->hasParam("leader_mac", true))
            newCfg.leader_mac = request->getParam("leader_mac", true)->value();

        if (request->hasParam("report_times", true)) {
            newCfg.report_times.clear();
            String raw = request->getParam("report_times", true)->value();
            int start = 0;
            while (start < raw.length()) {
                int comma = raw.indexOf(',', start);
                if (comma < 0) comma = raw.length();
                String token = raw.substring(start, comma);
                token.trim();
                if (!token.isEmpty()) newCfg.report_times.push_back(token);
                start = comma + 1;
            }
        }

        bool ok = _saveCb ? _saveCb(newCfg) : false;
        request->send(ok ? 200 : 500, "text/plain",
                      ok ? "Saved. Reboot recommended." : "Save failed");
    });
}

String WebPortal::htmlPage() {
    return R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Fridge Leader Manager</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root {
        --primary: #007bff;
        --success: #28a745;
        --warning: #ffc107;
        --danger: #dc3545;
        --bg: #f4f7f9;
        --card: #ffffff;
    }
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: var(--bg); color: #333; }
    .container { max-width: 1000px; margin: 0 auto; }
    .card { background: var(--card); padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.05); border: 1px solid #eef0f2; }
    h2 { margin-top: 0; font-size: 1.25rem; color: var(--primary); border-bottom: 2px solid var(--bg); padding-bottom: 10px; }
    
    /* Table Styling */
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th { text-align: left; padding: 12px; background: var(--bg); font-size: 0.85rem; text-transform: uppercase; letter-spacing: 1px; }
    td { padding: 12px; border-bottom: 1px solid #f0f0f0; vertical-align: middle; }
    
    /* Battery Icon Logic */
    .battery-container { display: flex; align-items: center; gap: 8px; font-weight: bold; }
    .battery-out { width: 35px; height: 18px; border: 2px solid #555; border-radius: 3px; position: relative; padding: 1px; }
    .battery-out::after { content: ''; position: absolute; right: -4px; top: 4px; width: 3px; height: 6px; background: #555; border-radius: 0 1px 1px 0; }
    .battery-level { height: 100%; border-radius: 1px; transition: width 0.3s ease, background 0.3s ease; }
    
    /* Form Styling */
    label { display: block; margin-top: 12px; font-weight: 600; font-size: 0.9rem; }
    input { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ddd; border-radius: 6px; box-sizing: border-box; }
    button { background: var(--primary); color: white; border: none; padding: 12px 20px; border-radius: 6px; cursor: pointer; margin-top: 15px; font-weight: bold; width: 100%; }
    button:hover { opacity: 0.9; }
    
    .status-item { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #f9f9f9; }
    .error-text { color: var(--danger); font-weight: bold; }
</style>
</head>
<body>
<div class="container">
    <div class="card">
        <h2>Leader Status</h2>
        <div class="status-item"><span>Device MAC:</span> <b id="deviceMac">--:--</b></div>
        <div class="status-item"><span>Queue size:</span> <span id="queueSize">0</span></div>
        <div class="status-item"><span>Last error:</span> <span id="lastError" class="">none</span></div>
    </div>

    <div class="card">
        <h2>Follower Dashboard</h2>
        <div style="overflow-x:auto;">
            <table>
                <thead>
                    <tr><th>ID</th><th>Client</th><th>Temp</th><th>Battery</th><th>MAC</th><th>Seen</th></tr>
                </thead>
                <tbody id="nodes"></tbody>
            </table>
        </div>
    </div>

    <div class="card">
        <h2>Settings</h2>
        <form method="POST" action="/save">
            <label>Device Name</label><input name="device_name" value=")HTML" + _cfg.device_name + R"HTML(">
            <label>WiFi SSID</label><input name="wifi_ssid" value=")HTML" + _cfg.wifi_ssid + R"HTML(">
            <label>WiFi Password</label><input name="wifi_pass" type="password">
            <label>Report Times (HH:MM, ...)</label><input name="report_times" value="10:00,22:00">
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</div>

<script>
function getBatteryStyle(volt) {
    // Calcul approximatif pour une batterie Li-po / Li-ion (3.0V à 4.2V)
    let percent = Math.min(100, Math.max(0, ((volt - 3.3) / (4.2 - 3.3)) * 100));
    let color = 'var(--success)';
    if (percent < 20) color = 'var(--danger)';
    else if (percent < 50) color = 'var(--warning)';
    return { p: percent, c: color };
}

async function refreshNodes() {
    try {
        const res = await fetch('/api/nodes');
        const data = await res.json();
        const tbody = document.getElementById('nodes');
        tbody.innerHTML = '';
        data.forEach(n => {
            const t = Number.isFinite(n.temp) ? n.temp.toFixed(1) + '°C' : 'ERR';
            const v = Number.isFinite(n.volt) ? n.volt : 0;
            const bat = getBatteryStyle(v);
            
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><b>${n.id}</b></td>
                <td>${n.client}</td>
                <td>${t}</td>
                <td>
                    <div class="battery-container">
                        <div class="battery-out">
                            <div class="battery-level" style="width: ${bat.p}%; background: ${bat.c};"></div>
                        </div>
                        <small>${v.toFixed(2)}V</small>
                    </div>
                </td>
                <td style="font-size:0.8rem; color:#888">${n.mac}</td>
                <td>${(n.last_seen_ms / 1000).toFixed(0)}s</td>
            `;
            tbody.appendChild(tr);
        });
    } catch(e) {}
}

async function refreshStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        document.getElementById('queueSize').textContent = data.queue_size;
        document.getElementById('deviceMac').textContent = data.device_mac;
        const errEl = document.getElementById('lastError');
        errEl.textContent = data.last_error || 'none';
        errEl.className = data.last_error ? 'error-text' : '';
    } catch(e) {}
}

setInterval(() => { refreshNodes(); refreshStatus(); }, 2000);
refreshNodes();
refreshStatus();
</script>
</body>
</html>
)HTML";
}