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

    // API pour les noeuds déjà configurés
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

    // API pour lister les capteurs détectés en mode installation (Provisioning)
    _server.on("/api/provision/list", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        
        // Note: Accède à la liste des découvertes via le callback ou l'instance Leader
        // Pour l'exemple, on renvoie un tableau vide si non implémenté
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

        bool ok = _saveCb ? _saveCb(newCfg) : false;
        request->send(ok ? 200 : 500, "text/plain",
                      ok ? "Sauvegardé. Redémarrage requis." : "Erreur sauvegarde");
    });
}

String WebPortal::htmlPage() {
    return R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Fridge Leader - Installation</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #007bff; --success: #28a745; --warning: #ffc107; --danger: #dc3545; --bg: #f4f7f9; --card: #ffffff; }
    body { font-family: 'Segoe UI', sans-serif; margin: 0; padding: 20px; background: var(--bg); color: #333; }
    .container { max-width: 1000px; margin: 0 auto; }
    .card { background: var(--card); padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.05); }
    h2 { color: var(--primary); border-bottom: 2px solid var(--bg); padding-bottom: 10px; }
    table { width: 100%; border-collapse: collapse; }
    th { text-align: left; padding: 12px; background: var(--bg); }
    td { padding: 12px; border-bottom: 1px solid #eee; }
    .btn { background: var(--primary); color: white; border: none; padding: 8px 15px; border-radius: 4px; cursor: pointer; }
    .input-small { padding: 5px; border: 1px solid #ddd; border-radius: 4px; width: 120px; }
</style>
</head>
<body>
<div class="container">
    <div class="card">
        <h2>Détection Nouveaux Capteurs (Provisioning)</h2>
        <table>
            <thead><tr><th>MAC</th><th>Batt</th><th>Type</th><th>Action</th></tr></thead>
            <tbody id="provision-list"></tbody>
        </table>
    </div>

    <div class="card">
        <h2>Dashboard Followers</h2>
        <table>
            <thead><tr><th>ID</th><th>Nom</th><th>Temp</th><th>Batterie</th><th>Dernier signe</th></tr></thead>
            <tbody id="nodes"></tbody>
        </table>
    </div>

    <div class="card">
        <h2>Paramètres Leader</h2>
        <form method="POST" action="/save">
            <label>Nom Leader</label><br><input name="device_name" value=")HTML" + _cfg.device_name + R"HTML("><br>
            <label>WiFi SSID</label><br><input name="wifi_ssid" value=")HTML" + _cfg.wifi_ssid + R"HTML("><br>
            <button type="submit" class="btn" style="margin-top:10px">Sauvegarder</button>
        </form>
    </div>
</div>

<script>
async function refreshProvisioning() {
    try {
        const res = await fetch('/api/provision/list');
        const data = await res.json();
        const tbody = document.getElementById('provision-list');
        tbody.innerHTML = data.length ? '' : '<tr><td colspan="4">Aucun nouveau capteur détecté...</td></tr>';
        data.forEach(f => {
            tbody.innerHTML += `
                <tr>
                    <td>${f.mac}</td>
                    <td>${f.volt}V</td>
                    <td>${f.sensor}</td>
                    <td>
                        <input type="text" id="n-${f.mac}" class="input-small" placeholder="Nom">
                        <button class="btn" onclick="applyConfig('${f.mac}')">Installer</button>
                    </td>
                </tr>`;
        });
    } catch(e) {}
}

async function refreshNodes() {
    const res = await fetch('/api/nodes');
    const data = await res.json();
    const tbody = document.getElementById('nodes');
    tbody.innerHTML = '';
    data.forEach(n => {
        tbody.innerHTML += `<tr><td>${n.id}</td><td>${n.client}</td><td>${n.temp}°C</td><td>${n.volt}V</td><td>${(n.last_seen_ms/1000).toFixed(0)}s</td></tr>`;
    });
}

setInterval(() => { refreshNodes(); refreshProvisioning(); }, 3000);
</script>
</body>
</html>
)HTML";
}