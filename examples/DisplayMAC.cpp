/*
    Display MAC address of WiFi chip on ESP32-C5 to serial
*/

#include "Arduino.h"
#include "esp32-wifi-manager.h"

void setup() {
    Serial.begin(115200);
}

void loop() {
    ESP32WiFiManager::printMAC();
    delay(2000);
}