#include "web_portal.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "protocol_types.h"
#include "leader_service.h"
#include "logger.h"

WebPortal::WebPortal(uint16_t port) : _server(port) {}

void WebPortal::begin(const AppConfig& cfg,
                      SaveConfigCallback saveCb,
                      GetNodesCallback nodesCb,
                      GetStatusCallback statusCb,
                      GetDiscoveredCallback discoveredCb,
                      ProvisionCallback provisionCb) { 
    _cfg = cfg;
    _saveCb = saveCb;
    _nodesCb = nodesCb;
    _statusCb = statusCb;
    _discoveredCb = discoveredCb;
    _provisionCb = provisionCb;
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
        if (_discoveredCb) {
            auto discovered = _discoveredCb(); 
            for (const auto& f : discovered) {
                JsonObject o = arr.add<JsonObject>();
                o["mac"] = f.mac;
                o["volt"] = f.volt;
                o["sensor"] = f.sensorType;
            }
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



   _server.on("/api/provision/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (request->hasParam("mac") && request->hasParam("name")) {
        String targetMac = request->getParam("mac")->value();
        String nodeName = request->getParam("name")->value();
        
        // On récupère aussi la MAC du leader si elle est passée, 
        // sinon on utilise notre propre adresse MAC par défaut
        String leaderMac = request->hasParam("leader") ? 
                           request->getParam("leader")->value() : WiFi.macAddress();

        Logger::info("Provisioning request for " + targetMac + " with name: " + nodeName);

        // On appelle provisionNode avec tous les paramètres
        if (_provisionCb && _provisionCb(targetMac, nodeName)) {
            request->send(200, "text/plain", "OK");
        } else {
            request->send(500, "text/plain", "Error");
        }
    }
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
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Fridge Leader Portal</title>
    <style>
        body { font-family: sans-serif; background: #f4f7f9; color: #333; padding: 20px; }
        .container { max-width: 900px; margin: auto; }
        .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); margin-bottom: 20px; }
        h2 { color: #2c3e50; border-bottom: 2px solid #eee; padding-bottom: 10px; }
        table { width: 100%; border-collapse: collapse; }
        th, td { text-align: left; padding: 10px; border-bottom: 1px solid #eee; }
        .status-item { margin: 5px 0; }
        .input-small { padding: 5px; border: 1px solid #ccc; border-radius: 4px; width: 140px; }
        .btn { background: #3498db; color: white; border: none; padding: 7px 12px; border-radius: 4px; cursor: pointer; }
        .btn-red { background: #e74c3c; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; }
    </style>
</head>
<body>
<div class="container">
    <div class="card">
        <h2>Leader Status</h2>
        <div class="status-item"><span>Device MAC:</span> <b id="deviceMac">--:--</b></div>
        <div class="status-item"><span>Queue size:</span> <span id="queueSize">0</span></div>
        <div class="status-item"><span>Last error:</span> <span id="lastError">none</span></div>
    </div>

    <div class="card">
        <h2>Nouveaux Capteurs détectés</h2>
        <table>
            <thead><tr><th>MAC</th><th>Batt</th><th>Type</th><th>Nom / MAC Leader</th><th>Action</th></tr></thead>
            <tbody id="provision-list"></tbody>
        </table>
    </div>

    <div class="card">
        <h2>Follower Dashboard</h2>
        <table>
            <thead><tr><th>UID</th><th>Nom</th><th>Temp</th><th>Volt</th><th>Action</th></tr></thead>
            <tbody id="nodes-list"></tbody>
        </table>
    </div>

    <div class="card">
        <h2>Configuration WiFi & Leader</h2>
        <form action="/save" method="POST">
            <div class="form-group">
                <label>Nom du Leader :</label>
                <input type="text" name="device_name" value=")HTML" + _cfg.device_name + R"HTML(">
            </div>
            <div class="form-group">
                <label>WiFi SSID :</label>
                <input type="text" name="wifi_ssid" value=")HTML" + _cfg.wifi_ssid + R"HTML(">
            </div>
            <div class="form-group">
                <label>WiFi Password :</label>
                <input type="password" name="wifi_pass" value=")HTML" + _cfg.wifi_pass + R"HTML(">
            </div>
            <button type="submit" class="btn">Enregistrer & Sauvegarder</button>
        </form>
    </div>
</div>

<script>
let isTyping = false;

async function refreshData() {
    if (isTyping) return;
    try {
        // Status
        const resSt = await fetch('/api/status');
        const st = await resSt.json();
        document.getElementById('deviceMac').innerText = st.device_mac;
        document.getElementById('queueSize').innerText = st.queue_size;
        document.getElementById('lastError').innerText = st.last_error || "none";

        // Provisioning List (Appelle /api/provision/apply)
        const resProv = await fetch('/api/provision/list');
        const provs = await resProv.json();
        let provHtml = "";
        provs.forEach(p => {
            provHtml += `<tr>
                <td>${p.mac}</td>
                <td>${p.volt}V</td>
                <td>${p.sensor}</td>
                <td>
                    <input type="text" id="name-${p.mac}" placeholder="Nom" class="input-small" onfocus="isTyping=true" onblur="isTyping=false">
                    <input type="text" id="lmac-${p.mac}" value="${st.device_mac}" class="input-small">
                </td>
                <td><button class="btn" onclick="applyProvision('${p.mac}')">Installer</button></td>
            </tr>`;
        });
        document.getElementById('provision-list').innerHTML = provHtml || "<tr><td colspan='5'>Rien détecté</td></tr>";

        // Nodes List
        const resNodes = await fetch('/api/nodes');
        const nodes = await resNodes.json();
        let nodesHtml = "";
        nodes.forEach(n => {
            nodesHtml += `<tr>
                <td>${n.id}</td>
                <td><b>${n.client}</b></td>
                <td>${n.temp}°C</td>
                <td>${n.volt}V</td>
                <td><button class="btn btn-red">Supprimer</button></td>
            </tr>`;
        });
        document.getElementById('nodes-list').innerHTML = nodesHtml || "<tr><td colspan='5'>Aucun follower</td></tr>";
    } catch(e) {}
}

async function applyProvision(mac) {
    const name = document.getElementById('name-' + mac).value;
    const leader = document.getElementById('lmac-' + mac).value;
    if(!name) { alert("Nom requis"); return; }
    
    // Appel à l'API de provisionnement
    const res = await fetch(`/api/provision/apply?mac=${mac}&name=${name}&leader=${leader}`, { method: 'POST' });
    if(res.ok) {
        alert("Configuration envoyée au follower !");
        refreshData();
    } else {
        alert("Erreur d'envoi");
    }
}

setInterval(refreshData, 3000);
window.onload = refreshData;
</script>
</body>
</html>
)HTML";
}