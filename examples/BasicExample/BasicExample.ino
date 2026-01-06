#include "RoamingWiFiManager.h"

RoamingWiFiManager manager;

#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"
#endif

#ifndef ALIAS_URL
#define ALIAS_URL ""
#endif
#ifndef ADMIN_USER
#define ADMIN_USER ""
#define ADMIN_PASSWORD ""
#endif

const std::vector<NetworkCredentials> knownNetworks = {{WIFI_SSID,WIFI_PASSWORD},{"PhoneHotspot", "asdf1234"}, {"iotroam", "passroam"}};
const String adminUser = ADMIN_USER;
const String adminPassword = ADMIN_PASSWORD; // leave blank for no authentication
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

    if (adminPassword.isEmpty() || adminUser.isEmpty()) {
        html.replace("$AUTH_INFO$", "(no authentication)");
    } else {
        html.replace("$AUTH_INFO$", "(user: " + adminUser + ", pass: " + (showPasswordOnRootPage?adminPassword:"****") + ")");
    }
    request->send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    manager.init(knownNetworks, aliasUrl, adminUser, adminPassword);
    // The manager already set up the ESP32AsyncWebServer instance at manager.server, but we can add our own routes to it.
    manager.server.on("/", [] (AsyncWebServerRequest *request) {
        handleRoot(request);
    });    
    Serial.println("Roaming Wifi tester: Setup completed.");
}

void loop() {
    manager.loop(); // must be periodically called to handle wifi management tasks
}
