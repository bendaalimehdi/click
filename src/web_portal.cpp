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
            // ON RÉCUPÈRE LES STRINGS ICI
            String mac = request->getParam("mac")->value();
            String name = request->getParam("name")->value();
            
            // On les passe au callback
            if (_provisionCb && _provisionCb(mac, name)) { // Utilise les variables 'mac' et 'name' créées juste au dessus
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
    <title>Fridge Leader</title>
    <style>
        /* ... tes styles ... */
        .detected-row { background: #fffde7; }
    </style>
</head>
<body>
<div class="container">
    <div class="card">
        <h2>Nouveaux Capteurs détectés</h2>
        <table>
            <thead><tr><th>MAC</th><th>Batt</th><th>Type</th><th>Nom à donner</th><th>Action</th></tr></thead>
            <tbody id="provision-list"></tbody>
        </table>
    </div>
    </div>

<script>
// Empêcher le rafraîchissement si l'utilisateur écrit
let isTyping = false;

async function applyConfig(mac) {
    const name = document.getElementById('n-' + mac).value;
    if(!name) { alert("Donnez un nom !"); return; }
    
    const btn = document.querySelector(`button[onclick="applyConfig('${mac}')"]`);
    btn.disabled = true;
    btn.innerText = "Envoi...";

    try {
        const res = await fetch(`/api/provision/apply?mac=${mac}&name=${name}`, { method: 'POST' });
        if(res.ok) {
            alert("Configuration envoyée ! Le capteur va redémarrer.");
            refreshProvisioning();
        } else {
            alert("Erreur lors de l'envoi");
        }
    } catch(e) { alert("Erreur réseau"); }
    btn.disabled = false;
    btn.innerText = "Installer";
}

async function refreshProvisioning() {
    if (isTyping) return; // Stop refresh pendant la saisie
    try {
        const res = await fetch('/api/provision/list');
        const data = await res.json();
        const tbody = document.getElementById('provision-list');
        
        // On ne reconstruit que si le nombre de devices change ou si vide
        let html = "";
        data.forEach(f => {
            html += `
                <tr class="detected-row">
                    <td>${f.mac}</td>
                    <td>${f.volt}V</td>
                    <td>${f.sensor}</td>
                    <td><input type="text" id="n-${f.mac}" onfocus="isTyping=true" onblur="isTyping=false" class="input-small"></td>
                    <td><button class="btn" onclick="applyConfig('${f.mac}')">Installer</button></td>
                </tr>`;
        });
        if(html === "") html = "<tr><td colspan='5'>Aucun nouveau capteur...</td></tr>";
        tbody.innerHTML = html;
    } catch(e) {}
}

setInterval(refreshProvisioning, 3000);
</script>
</body>
</html>
)HTML";
}