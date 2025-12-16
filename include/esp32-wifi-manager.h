#pragma once
#include <ESPAsyncWebServer.h>

class ESP32WiFiManager {
    public:
        // Reads MAC address from wifi chip and prints it to Serial. Also sets mode to STA and 5GHz.
        static void printMAC();

        // Connects to the specified WiFi network. Typically, ssid is "iotroam".
        void init(String ssid, String password);
};
