#include <Arduino.h>
#include "RoamingWiFiManager.h"

RoamingWiFiManager manager;

#ifndef WIFI_CREDENTIALS
#define WIFI_CREDENTIALS {{"your-ssid","your-password"},{"your-ssid2","your-password2"}}
#endif

#ifndef ADMIN_CREDENTIALS
#define ADMIN_CREDENTIALS {"",""}
#endif

#ifndef ALIAS_URL
#define ALIAS_URL ""
#endif

const std::vector<NetworkCredentials> knownNetworks = WIFI_CREDENTIALS;
const std::pair<String, String> adminCredentials = ADMIN_CREDENTIALS;
const String aliasUrl = ALIAS_URL;
bool showPasswordOnRootPage = true;

// serve simple HTML page with link to wifi manager and its credentials
void handleRoot(AsyncWebServerRequest *request) {
    String html = 
R"rawliteral(<!DOCTYPE html>
<html lang="en">
    <head>
        <meta charset="utf-8">
        <title>Hello world</title>
    </head>
    <body>
        <h1>My website</h1>
        <p><a href="/wifi">WiFi manager</a> $AUTH_INFO$</p>
    </body>
</html>
)rawliteral";

    if (adminCredentials.first == "" || adminCredentials.second == "") {
        html.replace("$AUTH_INFO$", "(no authentication)");
    } else {
        html.replace("$AUTH_INFO$", "(user: " + adminCredentials.first + ", pass: " + (showPasswordOnRootPage?adminCredentials.second:"****") + ")");
    }
    request->send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    manager.init(knownNetworks, adminCredentials, aliasUrl);
    // The manager already set up the ESP32AsyncWebServer instance at manager.server, but we can add our own routes to it.
    manager.server.on("/", [] (AsyncWebServerRequest *request) {
        handleRoot(request);
    });    
    Serial.println("Roaming Wifi tester: Setup completed.");
}

void loop() {
    manager.loop(); // must be periodically called to handle wifi management tasks
}
