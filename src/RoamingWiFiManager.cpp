#include "RoamingWiFiManager.h"
#include <WiFi.h>
#include <algorithm>
#include <mbedtls/base64.h>

#include "../assets/WiFiPage.html" // contains the WIFI_HTML string

// Runtime debug level control
#define DBG_PRINT_L(minLevel, ...)  if (RoamingWiFiManager::debugLevel >= (minLevel)) { Serial.print(__VA_ARGS__); }
#define DBG_PRINTLN_L(minLevel, ...) if (RoamingWiFiManager::debugLevel >= (minLevel)) { Serial.println(__VA_ARGS__); }
#define DBG_PRINTF_L(minLevel, ...) if (RoamingWiFiManager::debugLevel >= (minLevel)) { Serial.printf(__VA_ARGS__); }

// Waveshare ESP32-C5 RGB LED wiring is reversed
#ifdef RED_GREEN_REVERSED
#define LED(R, G, B) if (RoamingWiFiManager::useLEDIndicator) { rgbLedWrite(RGB_BUILTIN, G, R, B); }
#else
#define LED(R, G, B) if (RoamingWiFiManager::useLEDIndicator) { rgbLedWrite(RGB_BUILTIN, R, G, B); }
#endif

#ifdef WIFI_DEBUG_LEVEL
int RoamingWiFiManager::debugLevel = WIFI_DEBUG_LEVEL;
#else
int RoamingWiFiManager::debugLevel = 1;
#endif

String RoamingWiFiManager::toString(ScanPurpose purpose) {
    switch (purpose) {
        case ScanPurpose::ManualFull:
            return "manualFull";
        case ScanPurpose::AutoFull:
            return "autoFull";
        case ScanPurpose::AutoRescanSingle:
            return "autoRescanSingle";
        case ScanPurpose::AutoRescanTestChannel:
            return "autoRescanTestChannel";
        case ScanPurpose::None:
            return "none";
        default:
            return "unknown";
    }
}

bool RoamingWiFiManager::parseBssid(const String& bssidStr, uint8_t bssid[6]) {
    return (sscanf(bssidStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]) == 6);
}


void RoamingWiFiManager::printMAC() {
    uint8_t mac[6];

    WiFi.mode(WIFI_STA);
    WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY);
    DBG_PRINTF_L(1, "ESP32 WiFi MAC Address: %s\n", WiFi.macAddress().c_str());
}


RoamingWiFiManager::RoamingWiFiManager(int port) : server(port) {
    // Constructor body can be empty or used for initialization if needed
}


void RoamingWiFiManager::loadScanSettings() {
    const bool hasAutoFullEn = wifiPrefs.isKey("autoFullEn");
    const bool hasAutoFullInt = wifiPrefs.isKey("autoFullIntSecF");
    const bool hasAutoRescanEn = wifiPrefs.isKey("autoRescanEn");
    const bool hasAutoRescanInt = wifiPrefs.isKey("autoRescIntSecF");

    // If keys exist, use them. Otherwise, use defaults.
    if (!hasAutoFullEn) {
        wifiPrefs.putBool("autoFullEn", false);
    }
    autoFullScanEnabled = wifiPrefs.getBool("autoFullEn", false);

    if (!hasAutoFullInt) {
        wifiPrefs.putFloat("autoFullIntSecF", 10.0f);
    }
    {
        float vSec = wifiPrefs.getFloat("autoFullIntSecF", 10.0f);
        if (!(vSec >= 0.1f && vSec <= 3600.0f)) {
            vSec = 10.0f;
        }
        autoFullScanIntervalSec = vSec;
    }

    if (!hasAutoRescanEn) {
        wifiPrefs.putBool("autoRescanEn", true);
    }
    autoRescanKnownEnabled = wifiPrefs.getBool("autoRescanEn", true);

    if (!hasAutoRescanInt) {
        wifiPrefs.putFloat("autoRescIntSecF", 1.0f);
    }
    {
        float vSec = wifiPrefs.getFloat("autoRescIntSecF", 1.0f);
        if (!(vSec >= 0.1f && vSec <= 3600.0f)) {
            vSec = 1.0f;
        }
        autoRescanKnownIntervalSec = vSec;
    }

    // Whether the automatic rescan sweep is limited to known networks only.
    // Default to true to preserve previous behavior.
    if (!wifiPrefs.isKey("autoRescKnOnly")) {
        wifiPrefs.putBool("autoRescKnOnly", true);
    }
    autoRescanKnownOnlySetting = wifiPrefs.getBool("autoRescKnOnly", true);

    // Whether to include a test-channel sweep after rescan completion.
    // Default to true for broader discovery.
    if (!wifiPrefs.isKey("autoRescTestCh")) {
        wifiPrefs.putBool("autoRescTestCh", true);
    }
    autoRescanTestChannels = wifiPrefs.getBool("autoRescTestCh", true);
}

void RoamingWiFiManager::loadStatusSettings() {
    if (!wifiPrefs.isKey("statusIntSecF")) wifiPrefs.putFloat("statusIntSecF", 0.5f);
    float vSec = wifiPrefs.getFloat("statusIntSecF", -1.0f);
    if (!(vSec >= 0.1f && vSec <= 3600.0f)) {
        vSec = 0.5f;
    }
    statusRefreshIntervalSec = vSec;

    if (!wifiPrefs.isKey("statusAutoEn")) wifiPrefs.putBool("statusAutoEn", true);
    statusAutoRefreshEnabled = wifiPrefs.getBool("statusAutoEn", true);
}

void RoamingWiFiManager::loadReconnectSettings() {
    if (!wifiPrefs.isKey("reconEn")) wifiPrefs.putBool("reconEn", true);
    autoReconnectEnabled = wifiPrefs.getBool("reconEn", true);

    if (!wifiPrefs.isKey("reconIntSecF")) wifiPrefs.putFloat("reconIntSecF", 5.0f);
    float vSec = wifiPrefs.getFloat("reconIntSecF", -1.0f);
    if (!(vSec >= 0.1f && vSec <= 3600.0f)) {
        vSec = 5.0f;
    }
    autoReconnectIntervalSec = vSec;
}

void RoamingWiFiManager::loadRoamSettings() {
    if (!wifiPrefs.isKey("roamAutoEn")) wifiPrefs.putBool("roamAutoEn", true);
    autoRoamEnabled = wifiPrefs.getBool("roamAutoEn", true);

    if (!wifiPrefs.isKey("roamDeltaDbmF")) wifiPrefs.putFloat("roamDeltaDbmF", 10.0f);
    float dDbm = wifiPrefs.getFloat("roamDeltaDbmF", -100.0f);
    // Accept 1..50 dBm as reasonable bounds
    if (!(dDbm >= 1.0f && dDbm <= 50.0f)) {
        dDbm = 10.0f;
    }
    autoRoamDeltaRssiDbm = dDbm;

    if (!wifiPrefs.isKey("roamSameSsid")) wifiPrefs.putBool("roamSameSsid", true);
    autoRoamSameSsidOnly = wifiPrefs.getBool("roamSameSsid", true);
}

void RoamingWiFiManager::loadDebugLevel() {
    if (!wifiPrefs.isKey("debugLevel")) wifiPrefs.putInt("debugLevel", 0);
    int level = wifiPrefs.getInt("debugLevel", 0);
    debugLevel = (level >= 0 && level <= 5) ? level : 0;
#ifdef WIFI_DEBUG_LEVEL
    if (debugLevel < WIFI_DEBUG_LEVEL) {
        debugLevel = WIFI_DEBUG_LEVEL;
        DBG_PRINTF_L(0, "WiFi: Debug level forced to %d by compile-time macro WIFI_DEBUG_LEVEL.\n", debugLevel);
    }
#endif
    DBG_PRINTF_L(0, "WiFi: Debug level set to %d\n", debugLevel);
}

void RoamingWiFiManager::loadNetworkInfo() {
    // Load last connected network info (may be absent on first boot)
    savedSSID = wifiPrefs.getString("savedSsid", "");
    savedBSSID = wifiPrefs.getString("savedBssid", "");
    savedChannel = wifiPrefs.getInt("savedChan", 0);
    // Only attempt fast reconnect if this flag was true on the previous boot.
    lastQuickReconnectSuccess = wifiPrefs.getBool("lastQuickOK", false);
}

bool RoamingWiFiManager::loadPersistedSettings() {
    bool hasPrefs = wifiPrefs.begin("wifi", false);
    if (!hasPrefs) {
        DBG_PRINTLN_L(1, "WiFi: Cannot start wifiPrefs.");
        return false;
    }
    DBG_PRINTF_L(1, "WiFi: wifiPrefs started. Number of free entries: %d\n", wifiPrefs.freeEntries());

    // We may have UI/settings persisted even if we don't yet have a saved network.
    // The return value of this function is used to indicate whether saved network
    // information is available (for fast reconnect gating).
    const bool haveSavedNetwork = wifiPrefs.isKey("savedSsid") && wifiPrefs.isKey("savedBssid") && wifiPrefs.isKey("savedChan");
    if (!haveSavedNetwork) {
        DBG_PRINTLN_L(1, "WiFi: No saved network found; loading other persisted settings only.");
    }

    // Load various settings groups
    loadScanSettings();
    loadStatusSettings();
    loadReconnectSettings();
    loadRoamSettings();
    loadDebugLevel();
    loadNetworkInfo();

    return haveSavedNetwork;
}


void RoamingWiFiManager::init(std::vector<NetworkCredentials> credentials, String bssidAliasesUrl, String adminUser, String adminPassword) {
    LED(50, 50, 50); // White

    this->bssidAliasesUrl = bssidAliasesUrl;
    _adminUser = adminUser;
    _adminPassword = adminPassword;
    knownNetworks = credentials;

    if (knownNetworks.size() == 1) {
        DBG_PRINTF_L(2,"WiFi manager: Initializing known network SSID: %s\n", knownNetworks[0].ssid);
    } else {
        DBG_PRINTF_L(2,"WiFi manager: Initializing any of %d known networks\n", (int)knownNetworks.size());
    }


    WiFi.onEvent(
        [this](arduino_event_id_t event,
                arduino_event_info_t info) {
            handleWiFiEvent(event, info);
        }
    );

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY);
    delay(100);

    String stationMac = WiFi.macAddress();
    DBG_PRINTF_L(0,"WiFi: Station MAC: %s\n", stationMac.c_str());

    const bool persistedSettingsLoaded = loadPersistedSettings();
    wifiPrefs.putBool("lastQuickOK", false); // on next startup it will be false, unless we manage to quick connect

    bool fastPathUsed = false;

    if (persistedSettingsLoaded && lastQuickReconnectSuccess && savedBSSID.length() > 0 && savedSSID.length() > 0) {
        String pw = getPasswordOfNetwork(savedSSID);
        if (pw.length() > 0) {
            DBG_PRINTF_L(2,"WiFi: Attempting fast reconnect without initial scan (SSID=%s, BSSID=%s, channel=%d) ...\n", savedSSID.c_str(), savedBSSID.c_str(), savedChannel);
            fastPathUsed = true;
            const bool ok = connectDirectSaved();
            if (!ok) {
                lastQuickReconnectSuccess = false;
            }
        } else {
            DBG_PRINTF_L(2,"WiFi: Saved SSID '%s' not in known networks; skipping fast reconnect.\n", savedSSID.c_str());
        }
    } else if (persistedSettingsLoaded && !lastQuickReconnectSuccess) {
        DBG_PRINTLN_L(2,"WiFi: Skipping fast reconnect because lastQuickReconnectSuccess is false.");
    }

    if (WiFi.isConnected()) {
        DBG_PRINTLN_L(1,"WiFi: Fast reconnect succeeded.");
        // Seed the scan list with the currently-connected network so the UI has
        // something immediately, even before the background async scan completes.
        if (fastPathUsed) {
            const String ssid = WiFi.SSID();
            const String bssid = WiFi.BSSIDstr();
            if (ssid.length() > 0 && bssid.length() > 0) {
                scannedNetworkList.clear();
                ScannedNetwork net;
                net.ssid = ssid;
                net.bssid = bssid;
                net.rssi = WiFi.RSSI();
                net.channel = (uint8_t)WiFi.channel();
                net.encryption = "Unknown";
                net.scanned = true;
                net.detected = true;
                net.known = isKnownSsid(ssid);
                scannedNetworkList.push_back(net);
                sortNetworks();
                lastNetworksScanTime = millis();
                lastNetworksScanType = "fastReconnect";
            }
        }
        lastAutoFullScanTime = millis();
        //scanPurpose = ScanPurpose::AutoFull;
        //scanNetworksFullAsync();
    } else {
        //DBG_PRINTLN_L(1,"WiFi: Fast reconnect failed."); // already printed in connectDirectSaved().
    }

    if (!WiFi.isConnected()) {
        if (fastPathUsed) {
            DBG_PRINTLN_L(2,"WiFi: Performing full initial scan...");
        } else {
            DBG_PRINTLN_L(2,"WiFi: No saved connection info; performing initial scan...");
        }
        LED(80, 10, 0); // Orange for scanning
        WiFi.disconnect(false);
        delay(500); // pause just to be sure
        int n = WiFi.scanNetworks();
        if (n > 0) {
            DBG_PRINTLN_L(2,"Networks scanned. Found " + String(n) + " networks.");
        } else if (n == WIFI_SCAN_RUNNING) {
            DBG_PRINTLN_L(2,"Scan already running.");
        } else if (n == WIFI_SCAN_FAILED) {
            DBG_PRINTLN_L(1,"WiFi scan failed.");
        } else {
            DBG_PRINTLN_L(1,"No networks found during scan. n=" + String(n));
        }
        LED(0, 0, 50); // Blue for processing scan results
        copyScannedNetworksToList(false);
        printNetworks();

        lastNetworksScanType = "full";

        // Count a full scan only after it completed and results were captured.
        if (n >= 0) {
            networkScanCount++;
        }

        lastAutoFullScanTime = millis();
        connectToStrongestNetwork();
    }

    DBG_PRINTLN_L(1,"Starting webserver...");
    setupWebServer();

    if (WiFi.isConnected()) {
        LED(0, 10, 0); // Green for connected
    } else {
        LED(10, 0, 0); // Red for not connected
    }
}

void RoamingWiFiManager::handleWiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
    String s;
    switch (event) { // see NetworkEvents.h for all event IDs
        case 100: s = "WiFi off"; break;
        case 101: s = "WiFi ready"; break;
        case 102: s = "Wifi scan done"; break;
        case 110: s = "Station started"; break;
        case 111: s = "Station stopped"; break;
        case 112: s = "Station connected"; break;
        case 113:
            s = "Station disconnected"; 
            stationDisconnected = true;
            break;
        case 115: s = "Station got IP"; break;
        default: s = "Other event"; break;
    }
    DBG_PRINTF_L(4,"WiFi Event: %d: %s\n", event, s.c_str());

    // Persist last-connected info when we have an IP.
    if (event == 115) { // ARDUINO_EVENT_WIFI_STA_GOT_IP
        wifiConnectedTime = millis();
        persistConnectedNetwork();

        // Remember the client IP that was assigned so it can be referenced later.
        const IPAddress ip = WiFi.localIP();
        const String ipStr = ip.toString();
        if (ipStr.length() > 0 && ipStr != "0.0.0.0") {
            _clientIpAddresses.push_back(ipStr);
        }
    }
}

bool RoamingWiFiManager::connectDirectSaved() {
    if (savedSSID.length() == 0) {
        return false;
    }

    String pw = getPasswordOfNetwork(savedSSID);
    if (pw.length() == 0) {
        DBG_PRINTF_L(1,"WiFi: Cannot fast reconnect; no password for SSID '%s'.\n", savedSSID.c_str());
        return false;
    }

    uint8_t bssid[6];
    bool haveBssid = parseBssid(savedBSSID, bssid);

    //WiFi.begin(savedSSID, pw);
    if (haveBssid && savedChannel > 0) {
        WiFi.begin(savedSSID, pw, savedChannel, bssid, true);
    } else if (savedChannel > 0) {
        WiFi.begin(savedSSID, pw, savedChannel);
    } else {
        WiFi.begin(savedSSID, pw);
    }

    int attempts = 0;
    const int startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 5000) && !stationDisconnected) {
        delay(100);
        DBG_PRINT_L(4,".");
        if (attempts % 2 == 0) {
            LED(40, 4, 0); // Orange
        } else {
            LED(0, 0, 0); // Off
        }
        attempts++;
    }
    DBG_PRINTLN_L(4,);

    if (stationDisconnected) {
        DBG_PRINTLN_L(1,"WiFi: Station got disconnected during fast reconnect attempt.");
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            DBG_PRINTLN_L(1,"WiFi: Fast reconnect succeeded!");
            DBG_PRINTF_L(0,"IP Address: %s\n", WiFi.localIP().toString().c_str());
            DBG_PRINTF_L(1,"AP BSSID: %s  Channel: %d  RSSI: %d dBm\n", WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI());
            LED(0, 10, 0); // Green, connected
            persistConnectedNetwork();
            return true;
        }
    }
    DBG_PRINTLN_L(1,"WiFi: Fast reconnect failed.");
    LED(10, 0, 0); // Red, not connected
    return false;
}

void RoamingWiFiManager::persistConnectedNetwork() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    const String ssid = WiFi.SSID();
    const String bssid = WiFi.BSSIDstr();
    const int channel = WiFi.channel();

    if (ssid.length() == 0 || bssid.length() == 0 || channel <= 0) {
        return;
    }

    savedSSID = ssid;
    savedBSSID = bssid;
    savedChannel = channel;

    wifiPrefs.putString("savedSsid", savedSSID);
    wifiPrefs.putString("savedBssid", savedBSSID);
    wifiPrefs.putInt("savedChan", savedChannel);

    // Mark fast reconnect as eligible next boot once we've successfully persisted a connection.
    lastQuickReconnectSuccess = true;
    wifiPrefs.putBool("lastQuickOK", true);

    DBG_PRINTF_L(2,"WiFi: Saved last connection (SSID=%s, BSSID=%s, channel=%d)\n", savedSSID.c_str(), savedBSSID.c_str(), savedChannel);
}



void RoamingWiFiManager::copyScannedNetworksToList(bool keepExisting) {
    int n = WiFi.scanComplete();
    if (n < 0) {
        DBG_PRINTLN_L(2,"WiFi: No completed scan to copy networks from.");
        return;
    } else {
        DBG_PRINTF_L(3,"WiFi: Copying %d scanned networks to internal list.\n", n);
    }

    if (keepExisting) {
        // Keep the existing list, but mark everything as not detected.
        // Then update entries found in this scan and add any new entries.
        for (auto& existing : scannedNetworkList) {
            // A full scan was performed, so every cached entry was part of the last scan attempt.
            // Even if it is not detected in this scan result, it is still considered "scanned".
            existing.scanned = true;
            existing.detected = false;
            //existing.rssi = -1000;
            existing.known = isKnownSsid(existing.ssid);
        }

        for (int i = 0; i < n; i++) {
            const String bssid = WiFi.BSSIDstr(i);
            if (bssid.length() == 0) {
                continue;
            }

            int existingIndex = -1;
            for (size_t j = 0; j < scannedNetworkList.size(); j++) {
                if (scannedNetworkList[j].bssid.equalsIgnoreCase(bssid)) {
                    existingIndex = (int)j;
                    break;
                }
            }

            if (existingIndex >= 0) {
                ScannedNetwork& entry = scannedNetworkList[(size_t)existingIndex];
                entry.ssid = WiFi.SSID(i);
                entry.bssid = bssid;
                entry.rssi = WiFi.RSSI(i);
                entry.channel = WiFi.channel(i);
                entry.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
                entry.scanned = true;
                entry.detected = true;
                entry.known = isKnownSsid(entry.ssid);
            } else {
                ScannedNetwork net;
                net.ssid = WiFi.SSID(i);
                net.bssid = bssid;
                net.rssi = WiFi.RSSI(i);
                net.channel = WiFi.channel(i);
                net.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
                net.scanned = true;
                net.detected = true;
                net.known = isKnownSsid(net.ssid);
                scannedNetworkList.push_back(net);
            }
        }
    } else {
        scannedNetworkList.clear();
        for (int i = 0; i < n; i++) {
            ScannedNetwork net;
            net.ssid = WiFi.SSID(i);
            net.bssid = WiFi.BSSIDstr(i);
            net.rssi = WiFi.RSSI(i);
            net.channel = WiFi.channel(i);
            net.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
            net.scanned = true;
            net.detected = true;
            net.known = isKnownSsid(net.ssid);
            scannedNetworkList.push_back(net);
        }
    }
    sortNetworks();

    lastNetworksScanTime = millis();

    // Free scan result memory.
    //WiFi.scanDelete();
}



JsonDocument RoamingWiFiManager::getScannedNetworksAsJsonDocument() {
    JsonDocument doc; // Clear existing document
    doc["scanAgeSec"] = (lastNetworksScanTime == 0) ? -1 : (int)((millis() - lastNetworksScanTime) / 1000);
    doc["scanCount"] = networkScanCount;
    doc["scanType"] = lastNetworksScanType;
    
    // Get currently connected network info for comparison
    const bool isConnected = (WiFi.status() == WL_CONNECTED);
    const String currentSsid = isConnected ? WiFi.SSID() : "";
    const String currentBssid = isConnected ? WiFi.BSSIDstr() : "";
    const int currentChannel = isConnected ? WiFi.channel() : 0;
    
    JsonArray scannedNetworks = doc["networks"].to<JsonArray>();
    for (const auto& net : scannedNetworkList) {
        JsonObject network = scannedNetworks.add<JsonObject>();
        network["ssid"] = net.ssid;
        network["bssid"] = net.bssid;
        network["rssi"] = net.rssi;
        network["channel"] = net.channel;
        network["encryption"] = net.encryption;
        network["scanned"] = net.scanned;
        network["detected"] = net.detected;
        network["known"] = net.known;
        
        // Determine if this is the currently connected network (same BSSID and channel)
        const bool matchBssid = isConnected && net.bssid.equalsIgnoreCase(currentBssid);
        const bool matchChannel = isConnected && (net.channel == currentChannel);
        network["connected"] = matchBssid && matchChannel;
        
        // Determine if this network has the same SSID as connected but different BSSID
        const bool sameSsid = isConnected && currentSsid.length() > 0 && net.ssid.equals(currentSsid);
        const bool differentBssid = !net.bssid.equalsIgnoreCase(currentBssid);
        network["sameSsidAsConnected"] = sameSsid && differentBssid;
    }

    // Expose previously assigned client IP addresses for UI display.
    JsonArray clientIps = doc["clientIps"].to<JsonArray>();
    for (const auto& ip : _clientIpAddresses) {
        if (!ip.isEmpty()) {
            clientIps.add(ip);
        }
    }
    return doc;
}


void RoamingWiFiManager::sortNetworks() {
    if (scannedNetworkList.empty()) {
        DBG_PRINTLN_L(3,"sortNetworks: No scanned networks available.");
        return;
    }
    // Desired order:
    // 1) Networks with SSID equal to currently connected SSID (if connected), sorted by RSSI
    // 2) Other known networks (excluding current SSID), sorted by RSSI
    // 3) Remaining (unknown) networks, sorted by RSSI

    const String currentSsid = WiFi.SSID();

    std::vector<ScannedNetwork> groupCurrentSsid;
    std::vector<ScannedNetwork> groupKnownOther;
    std::vector<ScannedNetwork> groupUnknown;

    groupCurrentSsid.reserve(scannedNetworkList.size());
    groupKnownOther.reserve(scannedNetworkList.size());
    groupUnknown.reserve(scannedNetworkList.size());

    for (const auto& net : scannedNetworkList) {
        const bool isCurrent = (currentSsid.length() > 0) && net.ssid.equals(currentSsid);
        if (isCurrent) {
            groupCurrentSsid.push_back(net);
            continue;
        }

        bool isKnown = false;
        for (const NetworkCredentials& known : knownNetworks) {
            if (net.ssid.equals(known.ssid)) {
                isKnown = true;
                break;
            }
        }
        if (isKnown) {
            groupKnownOther.push_back(net);
        } else {
            groupUnknown.push_back(net);
        }
    }

    auto rssiDesc = [](const ScannedNetwork& a, const ScannedNetwork& b) {
        return a.rssi > b.rssi;
    };

    std::sort(groupCurrentSsid.begin(), groupCurrentSsid.end(), rssiDesc);
    std::sort(groupKnownOther.begin(), groupKnownOther.end(), rssiDesc);
    std::sort(groupUnknown.begin(), groupUnknown.end(), rssiDesc);

    scannedNetworkList.clear();
    scannedNetworkList.reserve(groupCurrentSsid.size() + groupKnownOther.size() + groupUnknown.size());
    scannedNetworkList.insert(scannedNetworkList.end(), groupCurrentSsid.begin(), groupCurrentSsid.end());
    scannedNetworkList.insert(scannedNetworkList.end(), groupKnownOther.begin(), groupKnownOther.end());
    scannedNetworkList.insert(scannedNetworkList.end(), groupUnknown.begin(), groupUnknown.end());
}



void RoamingWiFiManager::printNetworks() {
    if (scannedNetworkList.empty()) {
        DBG_PRINTLN_L(1,"printNetworks: No scanned networks available.");
        return;
    }

    DBG_PRINTLN_L(1,"WiFi networks:");
    for (const auto& net : scannedNetworkList) {
        DBG_PRINTF_L(1,"SSID: %s, BSSID: %s, RSSI: %d, Channel: %d, %s\n",
                      net.ssid.c_str(),
                      net.bssid.c_str(),
                      net.rssi,
                      net.channel,
                      net.encryption.c_str());
    }
}

// Returns a ScannedNetwork struct if a suitable network is found
// If no network is found, an empty struct is returned.
ScannedNetwork RoamingWiFiManager::findBestNetworkVar() {
    int bestRSSI = -1000;
    String bestSSID = "";
    
    ScannedNetwork bestNetworkVar;
    
    // Find the strongest known network from cached data
    for (const auto& net : scannedNetworkList) {
        String scannedSSID = net.ssid;
        int rssi = net.rssi;
        if (!net.detected) {
            continue; // Skip networks not detected in the last scan
        }
        if (!net.known) {
            continue; // Skip unknown networks
        }
        
        if (rssi > bestRSSI) {
            bestRSSI = rssi;
            bestSSID = scannedSSID;
            bestNetworkVar = net;
        }
    }
    return bestNetworkVar;    
}


void RoamingWiFiManager::connectToStrongestNetwork() {
    DBG_PRINTLN_L(2,"WiFi: Connecting to strongest known network using cached data...");

    ScannedNetwork bestNetworkVar = findBestNetworkVar();

    if (bestNetworkVar.isEmpty()) {
        DBG_PRINTLN_L(2,"WiFi: None of scanned networks are known.");
        // Set LED to red when no known networks found
        LED(10, 0, 0); // Red for no networks found
        return;
    }

    const char* bssidStr = bestNetworkVar.bssid.c_str();
    int32_t channel = bestNetworkVar.channel;
    
    // Parse BSSID string (format: "XX:XX:XX:XX:XX:XX") into byte array
    uint8_t bssid[6];
    if (parseBssid(bssidStr, bssid)) {
        DBG_PRINTF_L(2,"WiFi: Connecting to %s (RSSI: %d, BSSID: %s, channel: %d)\n", 
                        bestNetworkVar.ssid.c_str(), bestNetworkVar.rssi, bssidStr, channel);
        WiFi.begin(bestNetworkVar.ssid, getPasswordOfNetwork(bestNetworkVar.ssid), 
                    channel, bssid, true);
    } else {
        DBG_PRINTF_L(2,"WiFi: Connecting to %s (RSSI: %d) - BSSID parse failed, using channel only\n", 
                        bestNetworkVar.ssid.c_str(), bestNetworkVar.rssi);
        WiFi.begin(bestNetworkVar.ssid, getPasswordOfNetwork(bestNetworkVar.ssid), 
                    channel);
    }
    // Wait for connection (with timeout)
    int attempts = 0;
    int startTimeMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis()-startTimeMs < 5000)) {
        delay(100);
        DBG_PRINT_L(4,".");
        if (attempts%2==0) {
            LED(40, 4, 0); // Orange
        } else {
            LED(0, 0, 0); // Off
        }
        attempts++;
    }
    DBG_PRINTLN_L(4,);
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnectedTime = millis();
        DBG_PRINTLN_L(1,"WiFi: Connected successfully!");
        DBG_PRINTF_L(0,"IP Address: %s\n", WiFi.localIP().toString().c_str());
        // Print device MAC and connected BSSID info
        DBG_PRINTF_L(1,"Station MAC: %s\n", WiFi.macAddress().c_str());
        DBG_PRINTF_L(1,"AP BSSID: %s  Channel: %d  RSSI: %d dBm\n", WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI());
        // Set LED to green when connected
        LED(0, 10, 0); // Green for connected
    } else {
        DBG_PRINTLN_L(1,"WiFi: Connection failed!");
        // Keep LED red for failed connection
        LED(10, 0, 0); // Red for connection failed
    }
    lastConnectAttemptTime = millis();
}

void RoamingWiFiManager::connectToTargetNetwork(const String& ssid, const String& bssid, int channel) {
    const String targetSsid = String(ssid);
    const String targetBssid = String(bssid);
    const int targetChannel = channel;

    if (targetSsid.isEmpty()) {
        DBG_PRINTLN_L(2,"WiFi: connectToTargetNetwork: empty SSID; ignoring.");
        return;
    }

    DBG_PRINTF_L(2,
        "WiFi: Connecting to target SSID='%s' BSSID='%s' channel=%d\n",
        targetSsid.c_str(),
        targetBssid.c_str(),
        targetChannel
    );


    String password = getPasswordOfNetwork(targetSsid);
    if (password.length() == 0) {
        // Might be an open network; attempt without password.
        DBG_PRINTF_L(2,"WiFi: No password for SSID '%s'; attempting open connection.\n", targetSsid.c_str());
        WiFi.begin(targetSsid.c_str());
    } else {
        uint8_t bssidBytes[6];
        const bool haveBssid = parseBssid(targetBssid, bssidBytes);

        if (haveBssid && targetChannel > 0) {
            WiFi.begin(targetSsid, password, targetChannel, bssidBytes, true);
        } else if (targetChannel > 0) {
            WiFi.begin(targetSsid, password, targetChannel);
        } else {
            WiFi.begin(targetSsid, password);
        }
    }

    int attempts = 0;
    int startTimeMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTimeMs < 5000)) {
        delay(100);
        DBG_PRINT_L(4,".");
        if (attempts % 2 == 0) {
            LED(40, 4, 0); // Orange
        } else {
            LED(0, 0, 0); // Off
        }
        attempts++;
    }
    DBG_PRINTLN_L(4,);

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnectedTime = millis();
        DBG_PRINTLN_L(1,"WiFi: Connected successfully!");
        DBG_PRINTF_L(0,"IP Address: %s\n", WiFi.localIP().toString().c_str());
        DBG_PRINTF_L(1,"Station MAC: %s\n", WiFi.macAddress().c_str());
        DBG_PRINTF_L(1,"AP BSSID: %s  Channel: %d  RSSI: %d dBm\n", WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI());
        LED(0, 10, 0); // Green for connected
    } else {
        DBG_PRINTLN_L(1,"WiFi: Connection failed!");
        LED(10, 0, 0); // Red for connection failed
    }
    lastConnectAttemptTime = millis();
}

void RoamingWiFiManager::sendUnauthorized(AsyncWebServerRequest *request, const char* message) {
    const char* body = (message && *message) ? message : "Unauthorized";
    AsyncWebServerResponse *response = request->beginResponse(401, "text/html", body);
    response->addHeader("WWW-Authenticate", "Basic realm=\"WiFi Manager\"");
    request->send(response);
}

bool RoamingWiFiManager::checkHttpAuth(AsyncWebServerRequest *request) {
    // If no credentials configured, allow all requests
    if (_adminUser.length() == 0 || _adminPassword.length() == 0) {
        return true;
    }

    // Check for Authorization header
    if (!request->hasHeader("Authorization")) {
        DBG_PRINTLN_L(2,"HTTP Auth: Missing Authorization header");
        sendUnauthorized(request, "Unauthorized");
        return false;
    }

    String authHeader = request->header("Authorization");
    if (!authHeader.startsWith("Basic ")) {
        DBG_PRINTLN_L(2,"HTTP Auth: Invalid Authorization header format");
        sendUnauthorized(request, "Unauthorized");
        return false;
    }

    // Extract base64 portion
    String base64Creds = authHeader.substring(6); // Remove "Basic " prefix
    
    // Decode base64: base64Creds should be "username:password" in base64
    size_t outlen = 0;
    unsigned char decoded[256]; // Buffer for decoded credentials
    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &outlen, 
                                     (const unsigned char*)base64Creds.c_str(), 
                                     base64Creds.length());
    
    if (ret != 0 || outlen == 0) {
        DBG_PRINTLN_L(2,"HTTP Auth: Base64 decode failed");
        sendUnauthorized(request, "Unauthorized - Decode error");
        return false;
    }
    
    // Null-terminate the decoded string
    decoded[outlen] = '\0';
    String decodedCreds((char*)decoded);
    
    // Expected format: "username:password"
    String expectedCreds = _adminUser + ":" + _adminPassword;
    
    if (decodedCreds != expectedCreds) {
        DBG_PRINTF_L(2,"HTTP Auth: Invalid credentials\n");
        sendUnauthorized(request, "Unauthorized - Invalid credentials");
        return false;
    }

    DBG_PRINTLN_L(5,"HTTP Auth: Valid credentials");
    return true;
}

void RoamingWiFiManager::setupStatusEndpoints() {
    // WiFi status endpoint
    server.on("/wifi/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        DBG_PRINTLN_L(5,"/wifi/status requested");
        request->send(200, "application/json", getWiFiStatus());
    });
}

void RoamingWiFiManager::setupScanEndpoints() {
    struct WifiScanRequestState {
        String body;
    };

    // Network scan endpoint
    // Body (optional JSON): { "mode": "complete" | "rescan" }
    // - complete: full async scan across all channels
    // - rescan: async sweep that re-scans existing cached networks only
    server.on(
        "/wifi/scan",
        HTTP_POST, [this](AsyncWebServerRequest *request) {
            // Final handler called after body is fully received
            // The actual response is sent in onBody handler below
        }, nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!checkHttpAuth(request)) return;
            WifiScanRequestState* st = reinterpret_cast<WifiScanRequestState*>(request->_tempObject);
            if (!st) {
                // first time onBody is called for this request, so make st object
                DBG_PRINTLN_L(2,"/wifi/scan: creating new WifiScanRequestState");
                st = new WifiScanRequestState();
                request->_tempObject = st;
            }
            //st->hasBody = true;
            for (size_t i = 0; i < len; i++) { // add data to body
                st->body += (char)data[i];
            }
            if (index + len != total) { // not the last chunk
                DBG_PRINTLN_L(3,"/wifi/scan: not last chunk of body");
                return;
            }

            // Parse final body.
            if (st->body.length() == 0) {
                request->send(400, "application/json", "{\"message\":\"Body is empty\"}");
                delete st;
                request->_tempObject = nullptr;
                return;
            }

            JsonDocument doc;
            if (!tryParseJson(st->body, doc, request)) {
                delete st;
                request->_tempObject = nullptr;
                return;
            }

            bool rescanOnly = false;
            String mode = doc["mode"] | "";
            if (mode == "rescan") {
                rescanOnly = true;
            } else if (mode == "complete") {
                rescanOnly = false;
            } else {
                sendJsonError(request, 400, "Invalid mode");
                delete st;
                request->_tempObject = nullptr;
                return;
            }

            if (rescanOnly && !scannedNetworkList.empty()) {
                // Only rescan existing networks
                autoRescanActive = false;
                const bool started = startAutoRescanNext(false);
                if (!started) {
                    request->send(200, "application/json", "{\"message\":\"Scan already in progress or nothing to rescan\"}");
                } else {
                    request->send(200, "application/json", "{\"message\":\"Rescan existing networks started\"}");
                }
            } else {
                // Full scan across all channels, async.
                scanPurpose = ScanPurpose::ManualFull;
                scanNetworksFullAsync();
                DBG_PRINTLN_L(2,"/wifi/scan: manual full async scan");
                request->send(200, "application/json", "{\"message\":\"Full async scan started\"}");
            }
            delete st;
            request->_tempObject = nullptr;
        }
    );

    // Get networks endpoint
    server.on("/wifi/networks", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        DBG_PRINTLN_L(5,"/wifi/networks requested");
        String s = getScannedNetworksAsJsonDocument().as<String>();
        request->send(200, "application/json", s);
    });
}

void RoamingWiFiManager::setupConnectionEndpoints() {
    // Connect to strongest endpoint
    server.on("/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        connectionRequested = true; // Set flag instead of calling function directly
        DBG_PRINTLN_L(2,"/wifi/connect requested");
        request->send(200, "application/json", "{\"message\":\"Connection request queued\"}");
    }); 

    // Connect to a specific network endpoint
    server.on("/wifi/connectTarget", HTTP_POST, [this](AsyncWebServerRequest *request) {
            // Final handler called after body is fully received
            // The actual response is sent in onBody handler below
        }, nullptr, 
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }

        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }

        String ssid = doc["ssid"] | "";
        String bssid = doc["bssid"] | "";
        int channel = doc["channel"] | 0;

        if (ssid.length() == 0) {
            request->send(400, "application/json", "{\"message\":\"Missing ssid\"}");
            return;
        }

        connectionTargetSSID = ssid;
        connectionTargetBSSID = bssid;
        connectionTargetChannel = channel;
        connectionTargetRequested = true;

        DBG_PRINTF_L(2,"/wifi/connectTarget queued ssid='%s' bssid='%s' channel=%d\n", ssid.c_str(), bssid.c_str(), channel);

        JsonDocument resp;
        resp["message"] = "Target connection request queued";
        resp["ssid"] = connectionTargetSSID;
        resp["bssid"] = connectionTargetBSSID;
        resp["channel"] = connectionTargetChannel;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    // Force disconnect endpoint (useful for testing auto-reconnect)
    server.on("/wifi/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        DBG_PRINTLN_L(2,"/wifi/disconnect requested");

        // Disconnect but keep credentials so auto-reconnect can re-use them.
        WiFi.disconnect();
        lastAutoReconnectAttemptTime = millis();
        autoReconnectAttemptCount = 0;

        request->send(200, "application/json", "{\"message\":\"Disconnect requested\"}");
    });
}

void RoamingWiFiManager::setupSettingsEndpoints() {
    // WiFi settings endpoints
    server.on("/wifi/autoscan", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Auto-scan setting updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }

        // New dual-toggle autoscan settings
        bool fullEnabled = doc["fullEnabled"] | autoFullScanEnabled;
        float fullIntervalSec = doc["fullIntervalSec"] | autoFullScanIntervalSec;
        if (!(fullIntervalSec >= 0.1f && fullIntervalSec <= 3600.0f)) {
            sendJsonError(request, 400, "fullIntervalSec out of range (0.1..3600)");
            return;
        }

        bool rescanEnabled = doc["rescanEnabled"] | autoRescanKnownEnabled;
        float rescanIntervalSec = doc["rescanIntervalSec"] | autoRescanKnownIntervalSec;
        bool rescanKnownOnly = doc["rescanKnownOnly"] | autoRescanKnownOnlySetting;
        bool rescanTestChannels = doc["rescanTestChannels"] | autoRescanTestChannels;
        if (!(rescanIntervalSec >= 0.1f && rescanIntervalSec <= 3600.0f)) {
            sendJsonError(request, 400, "rescanIntervalSec out of range (0.1..3600)");
            return;
        }

        DBG_PRINTF_L(2,
            "WiFi: Auto full-scan %s (%.1f sec), auto rescan %s (%.1f sec), known-only=%s, test-channels=%s\n",
            autoFullScanEnabled ? "enabled" : "disabled",
            (double)autoFullScanIntervalSec,
            autoRescanKnownEnabled ? "enabled" : "disabled",
            (double)autoRescanKnownIntervalSec,
            rescanKnownOnly ? "true" : "false",
            rescanTestChannels ? "true" : "false"
        );


        autoFullScanEnabled = fullEnabled;
        autoFullScanIntervalSec = fullIntervalSec;
        autoRescanKnownEnabled = rescanEnabled;
        autoRescanKnownIntervalSec = rescanIntervalSec;
        autoRescanKnownOnlySetting = rescanKnownOnly;
        autoRescanTestChannels = rescanTestChannels;

        wifiPrefs.putBool("autoFullEn", autoFullScanEnabled);
        wifiPrefs.putFloat("autoFullIntSecF", autoFullScanIntervalSec);
        wifiPrefs.putBool("autoRescanEn", autoRescanKnownEnabled);
        wifiPrefs.putFloat("autoRescIntSecF", autoRescanKnownIntervalSec);
        wifiPrefs.putBool("autoRescKnOnly", autoRescanKnownOnlySetting);
        wifiPrefs.putBool("autoRescTestCh", autoRescanTestChannels);

        // Reset any in-progress rescan sequence when settings change.
        autoRescanActive = false;
        autoRescanIndex = 0;
        autoRescanTargetBssid = "";
        autoRescanTargetChannel = 0;
        autoRescanKnownOnly = false;
        if (scanPurpose == ScanPurpose::AutoRescanSingle) {
            scanPurpose = ScanPurpose::None;
        }


        JsonDocument resp;
        resp["message"] = "Auto-scan setting updated";
        resp["fullEnabled"] = autoFullScanEnabled;
        resp["fullIntervalSec"] = autoFullScanIntervalSec;
        resp["rescanEnabled"] = autoRescanKnownEnabled;
        resp["rescanIntervalSec"] = autoRescanKnownIntervalSec;
        resp["rescanKnownOnly"] = autoRescanKnownOnlySetting;
        resp["rescanTestChannels"] = autoRescanTestChannels;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    server.on("/wifi/statusRefreshInterval", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Status refresh interval updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }

        float intervalSec = doc["intervalSec"] | statusRefreshIntervalSec;
        if (!(intervalSec >= 0.1f && intervalSec <= 3600.0f)) {
            sendJsonError(request, 400, "intervalSec out of range (0.1..3600)");
            return;
        }

        statusRefreshIntervalSec = intervalSec;
        wifiPrefs.putFloat("statusIntSecF", statusRefreshIntervalSec);
        DBG_PRINTF_L(2,"WiFi: Status refresh interval set to %.1f sec\n", statusRefreshIntervalSec);

        JsonDocument resp;
        resp["message"] = "Status refresh interval updated";
        resp["intervalSec"] = statusRefreshIntervalSec;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    server.on("/wifi/statusAutoRefreshEnabled", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Status auto-refresh enabled updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        // we assume that full body has been sent at once
        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }


        bool enabled = doc["enabled"] | true;
        statusAutoRefreshEnabled = enabled;
        wifiPrefs.putBool("statusAutoEn", statusAutoRefreshEnabled);

        DBG_PRINTF_L(2,"WiFi: Status auto-refresh %s\n", statusAutoRefreshEnabled ? "enabled" : "disabled");

        JsonDocument resp;
        resp["message"] = "Status auto-refresh enabled updated";
        resp["enabled"] = statusAutoRefreshEnabled;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    // Restore default settings endpoint
    server.on("/wifi/restoreDefaults", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        // Apply in-memory defaults
        autoFullScanEnabled = false;
        autoFullScanIntervalSec = 10.0f;
        autoRescanKnownEnabled = true;
        autoRescanKnownIntervalSec = 1.0f;
        autoRescanKnownOnlySetting = true;
        autoRescanTestChannels = true;
        statusRefreshIntervalSec = 0.5f;
        statusAutoRefreshEnabled = true;
        autoReconnectEnabled = true;
        autoReconnectIntervalSec = 5.0f;
        autoRoamEnabled = true;
        autoRoamDeltaRssiDbm = 10.0f;
        autoRoamSameSsidOnly = true;
        bssidAliasesUrl = "";

        // Persist defaults to NVS
        wifiPrefs.putBool("autoFullEn", autoFullScanEnabled);
        wifiPrefs.putFloat("autoFullIntSecF", autoFullScanIntervalSec);
        wifiPrefs.putBool("autoRescanEn", autoRescanKnownEnabled);
        wifiPrefs.putFloat("autoRescIntSecF", autoRescanKnownIntervalSec);
        wifiPrefs.putBool("autoRescKnOnly", autoRescanKnownOnlySetting);
        wifiPrefs.putBool("autoRescTestCh", autoRescanTestChannels);
        wifiPrefs.putFloat("statusIntSecF", statusRefreshIntervalSec);
        wifiPrefs.putBool("statusAutoEn", statusAutoRefreshEnabled);
        wifiPrefs.putUInt("reconEn", autoReconnectEnabled ? 1 : 0);
        wifiPrefs.putBool("reconEn", autoReconnectEnabled);
        wifiPrefs.putFloat("reconIntSecF", autoReconnectIntervalSec);
        wifiPrefs.putBool("roamAutoEn", autoRoamEnabled);
        wifiPrefs.putFloat("roamDeltaDbmF", autoRoamDeltaRssiDbm);
        wifiPrefs.putBool("roamSameSsid", autoRoamSameSsidOnly);
        wifiPrefs.putInt("debugLevel", debugLevel);

        // Reset any in-progress scan/rescan sequences
        autoRescanActive = false;
        autoRescanIndex = 0;
        autoRescanTargetBssid = "";
        autoRescanTargetChannel = 0;
        autoRescanKnownOnly = false;
        if (scanPurpose == ScanPurpose::AutoRescanSingle || scanPurpose == ScanPurpose::AutoRescanTestChannel) {
            scanPurpose = ScanPurpose::None;
        }

        JsonDocument resp;
        resp["message"] = "Defaults restored";
        // Include current settings for UI refresh
        resp["autoFullScanEnabled"] = autoFullScanEnabled;
        resp["autoFullScanIntervalSec"] = autoFullScanIntervalSec;
        resp["autoRescanKnownEnabled"] = autoRescanKnownEnabled;
        resp["autoRescanKnownIntervalSec"] = autoRescanKnownIntervalSec;
        resp["autoRescanKnownOnly"] = autoRescanKnownOnlySetting;
        resp["autoRescanTestChannels"] = autoRescanTestChannels;
        resp["statusRefreshIntervalSec"] = statusRefreshIntervalSec;
        resp["statusAutoRefreshEnabled"] = statusAutoRefreshEnabled;
        resp["autoReconnectEnabled"] = autoReconnectEnabled;
        resp["autoReconnectIntervalSec"] = autoReconnectIntervalSec;
        resp["autoRoamEnabled"] = autoRoamEnabled;
        resp["autoRoamDeltaRssiDbm"] = autoRoamDeltaRssiDbm;
        resp["autoRoamSameSsidOnly"] = autoRoamSameSsidOnly;
        resp["debugLevel"] = debugLevel;
        resp["bssidAliasesUrl"] = bssidAliasesUrl;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    // Auto-roam settings: enable + delta RSSI threshold + same-SSID constraint
    server.on("/wifi/autoRoam", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Auto-roam setting updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }

        bool enabled = doc["enabled"] | autoRoamEnabled;
        float deltaDbm = doc["deltaDbm"] | autoRoamDeltaRssiDbm;
        bool sameSsidOnly = doc["sameSsidOnly"] | autoRoamSameSsidOnly;
        // Validate bounds
        if (!(deltaDbm >= 1.0f && deltaDbm <= 50.0f)) {
            sendJsonError(request, 400, "deltaDbm out of range (1..50)");
            return;
        }

        autoRoamEnabled = enabled;
        autoRoamDeltaRssiDbm = deltaDbm;
        autoRoamSameSsidOnly = sameSsidOnly;
        wifiPrefs.putBool("roamAutoEn", autoRoamEnabled);
        wifiPrefs.putFloat("roamDeltaDbmF", autoRoamDeltaRssiDbm);
        wifiPrefs.putBool("roamSameSsid", autoRoamSameSsidOnly);

        JsonDocument resp;
        resp["message"] = "Auto-roam setting updated";
        resp["enabled"] = autoRoamEnabled;
        resp["deltaDbm"] = autoRoamDeltaRssiDbm;
        resp["sameSsidOnly"] = autoRoamSameSsidOnly;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    server.on("/wifi/autoreconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Auto-reconnect setting updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        JsonDocument doc;
        if (!tryParseJson(body, doc, request)) {
            return;
        }

        bool enabled = doc["enabled"] | autoReconnectEnabled;
        float intervalSec = doc["intervalSec"] | autoReconnectIntervalSec;
        if (!(intervalSec >= 0.1f && intervalSec <= 3600.0f)) {
            sendJsonError(request, 400, "intervalSec out of range (0.1..3600)");
            return;
        }

        autoReconnectEnabled = enabled;
        autoReconnectIntervalSec = intervalSec;
        wifiPrefs.putUInt("reconEn", autoReconnectEnabled ? 1 : 0);
        wifiPrefs.putBool("reconEn", autoReconnectEnabled);
        wifiPrefs.putFloat("reconIntSecF", autoReconnectIntervalSec);
        wifiPrefs.putUInt("reconIntSec", (uint32_t)(autoReconnectIntervalSec + 0.5f));
        lastAutoReconnectAttemptTime = 0;
        autoReconnectAttemptCount = 0;

        DBG_PRINTF_L(2,"WiFi: Auto-reconnect %s, interval %.1f sec\n", autoReconnectEnabled ? "enabled" : "disabled", (double)autoReconnectIntervalSec);

        JsonDocument resp;
        resp["message"] = "Auto-reconnect setting updated";
        resp["enabled"] = autoReconnectEnabled;
        resp["intervalSec"] = autoReconnectIntervalSec;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    server.on("/wifi/debugLevel", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        request->send(200, "application/json", "{\"message\":\"Debug level updated\"}");
    }, nullptr, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkHttpAuth(request)) return;
        String body = "";
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            request->send(400, "application/json", "{\"message\":\"Invalid JSON\"}");
            return;
        }

        int level = doc["level"] | debugLevel;
        if (level < 0 || level > 5) {
            sendJsonError(request, 400, "Debug level must be 0-5");
            return;
        }

        int oldLevel = debugLevel;
        debugLevel = level;
        wifiPrefs.putInt("debugLevel", debugLevel);

        DBG_PRINTF_L(1,"WiFi: Debug level set from %d to %d\n", oldLevel, debugLevel);

        JsonDocument resp;
        resp["message"] = "Debug level updated";
        resp["level"] = debugLevel;
        String result;
        serializeJson(resp, result);
        request->send(200, "application/json", result);
    });

    server.on("/wifi/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        JsonDocument doc;
        // New (preferred)
        doc["autoFullScanEnabled"] = autoFullScanEnabled;
        doc["autoFullScanIntervalSec"] = autoFullScanIntervalSec;
        doc["autoRescanKnownEnabled"] = autoRescanKnownEnabled;
        doc["autoRescanKnownIntervalSec"] = autoRescanKnownIntervalSec;
        doc["autoRescanKnownOnly"] = autoRescanKnownOnlySetting;
        doc["autoRescanTestChannels"] = autoRescanTestChannels;
        // Auto-roam fields
        doc["autoRoamEnabled"] = autoRoamEnabled;
        doc["autoRoamDeltaRssiDbm"] = autoRoamDeltaRssiDbm;
        doc["autoRoamSameSsidOnly"] = autoRoamSameSsidOnly;

        doc["statusRefreshIntervalSec"] = statusRefreshIntervalSec;
        doc["statusAutoRefreshEnabled"] = statusAutoRefreshEnabled;
        doc["autoReconnectEnabled"] = autoReconnectEnabled;
        doc["autoReconnectIntervalSec"] = autoReconnectIntervalSec;
        doc["debugLevel"] = debugLevel;
        doc["bssidAliasesUrl"] = bssidAliasesUrl;
        
        String result;
        serializeJson(doc, result);
        request->send(200, "application/json", result);
    });
}

void RoamingWiFiManager::setupMainEndpoint() {
    // Serve main wifi page, must be at last
    // handle root
    server.on("/wifi", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!checkHttpAuth(request)) return;
        if (request->url() == "/wifi" || request->url() == "/wifi/") {
            DBG_PRINTLN_L(4,"/wifi requested");

            // Debug: Print WIFI_HTML to serial if debug level is high enough
            /*
                        if (debugLevel >= 3) {
                            DBG_PRINTLN_L(3, "WIFI_HTML content:");
                            DBG_PRINTLN_L(3, WIFI_HTML);
                        }
            */

            // Send HTML from PROGMEM directly to avoid creating large String in RAM
            AsyncWebServerResponse *response = request->beginResponse(200, "text/html", WIFI_HTML);
            response->addHeader("Content-Encoding", "identity");
            
            // Calculate and set Content-Length header for proper HTTP response
            size_t contentLength = strlen_P(WIFI_HTML);
            response->addHeader("Content-Length", String(contentLength));
            response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

            request->send(response);
        } else {
            DBG_PRINTLN_L(2,"/wifi bad request");
            request->send(404, "text/plain", "/wifi : Bad request");
        }
    });    
    
    server.onNotFound([this](AsyncWebServerRequest *request) {
        DBG_PRINTF_L(2,"WiFi: 404 Not Found: %s\n", request->url().c_str());
        request->send(404, "text/plain", "URL '" + request->url() + "' not found. Go to /wifi for admin panel.");
    });
}

void RoamingWiFiManager::setupWebServer() {
    setupStatusEndpoints();
    setupScanEndpoints();
    setupConnectionEndpoints();
    setupSettingsEndpoints();
    setupMainEndpoint();
    
    server.begin();
    DBG_PRINTLN_L(1,"Webserver started.");
}

void RoamingWiFiManager::sendJsonError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument resp;
    resp["message"] = (message && *message) ? message : "Error";
    String result;
    serializeJson(resp, result);
    request->send(code, "application/json", result);
}

bool RoamingWiFiManager::tryParseJson(const String& body, JsonDocument& doc, AsyncWebServerRequest* request) {
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        sendJsonError(request, 400, "Invalid JSON");
        return false;
    }
    return true;
}

String RoamingWiFiManager::getWiFiStatus() {
    JsonDocument doc;
    
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"] = WiFi.SSID();
    doc["bssid"] = WiFi.BSSIDstr();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["mac"] = WiFi.macAddress();
    doc["channel"] = WiFi.channel();
    doc["saved_bssid"] = savedBSSID; // expose persisted BSSID
    doc["saved_ssid"] = savedSSID;
    doc["saved_channel"] = savedChannel;
    doc["autoRescanTargetChannel"] = autoRescanTestChannelList[autoRescanTestChannelIndex];
    
    // Calculate uptime
    if (wifiConnectedTime > 0 && WiFi.status() == WL_CONNECTED) {
        unsigned long uptimeMs = millis() - wifiConnectedTime;
        unsigned long uptimeSeconds = uptimeMs / 1000;
        unsigned long hours = uptimeSeconds / 3600;
        unsigned long minutes = (uptimeSeconds % 3600) / 60;
        unsigned long seconds = uptimeSeconds % 60;
        
        char uptimeStr[32];
        sprintf(uptimeStr, "%02luh %02lum %02lus", hours, minutes, seconds);
        doc["uptime"] = String(uptimeStr);
    } else {
        doc["uptime"] = "Not connected";
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RoamingWiFiManager::getPasswordOfNetwork(String ssid) {
    for (const NetworkCredentials& net : knownNetworks) {
        if (net.ssid.equals(ssid)) {
            return net.password;
        }
    }
    return "";
}

void RoamingWiFiManager::scanNetworksFullAsync() {
    autoRescanActive = false;
    autoRescanIndex = 0;
    autoRescanTargetBssid = "";
    autoRescanTargetChannel = 0;
    autoRescanSweepDidScan = false;

    if (!scanInProgress) {
        scanInProgress = true;
        WiFi.scanNetworks(true); // Async scan (all channels)
    } else {
        DBG_PRINTLN_L(2,"WiFi: Scan already in progress; cannot start another.");
    }
}

void RoamingWiFiManager::scanNetworkAsync(uint8_t channel, const uint8_t* bssid) {
    if (scanInProgress) {
        DBG_PRINTLN_L(2,"WiFi: Scan already in progress; cannot start another.");
        return;
    }

    scanInProgress = true;

    // Scan one channel only, a specific BSSID (may be null).
    // max_ms_per_chan kept short; we just want a quick refresh.
    WiFi.scanNetworks(true, true, false, 300, channel, nullptr, bssid);
}

static bool parseBssidStr(const String& bssidStr, uint8_t outBssid[6]) {
    if (bssidStr.length() < 17) {
        return false;
    }
    return (sscanf(bssidStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &outBssid[0], &outBssid[1], &outBssid[2], &outBssid[3], &outBssid[4], &outBssid[5]) == 6);
}

bool RoamingWiFiManager::isKnownSsid(const String& ssid) {
    if (ssid.length() == 0) {
        return false;
    }
    for (const NetworkCredentials& net : knownNetworks) {
        if (net.ssid.equals(ssid)) {
            return true;
        }
    }
    return false;
}

bool RoamingWiFiManager::startAutoRescanNext(bool knownOnly) {
    if (scanInProgress) {
        return false;
    }

    if (!autoRescanActive) { // it was not active, so initialize with first entry
        autoRescanActive = true;
        autoRescanIndex = 0;
        autoRescanSweepDidScan = false;
        autoRescanKnownOnly = knownOnly;

        // New sweep: do NOT clear scanned for eligible entries (known networks), because the UI may query
        // mid-sweep and we don't want to briefly report known networks as "not scanned".
        // Instead, only mark entries that are ineligible for this sweep as scanned=false.
        if (autoRescanKnownOnly) {
            for (auto& entry : scannedNetworkList) {
                if (!isKnownSsid(entry.ssid)) {
                    entry.scanned = false;
                }
            }
        }
    }

    // Skip entries that are not eligible for this sweep.
    while (autoRescanIndex < scannedNetworkList.size()) {
        if (!autoRescanKnownOnly) { // rescan all existing entries
            break;
        }
        const ScannedNetwork& candidate = scannedNetworkList[autoRescanIndex];
        if (isKnownSsid(candidate.ssid)) {
            break;
        }
        DBG_PRINTF_L(3,"WiFi: Auto-rescan skipping unknown network %s\n", candidate.ssid.c_str());
        scannedNetworkList[autoRescanIndex].scanned = false;
        autoRescanIndex++; // we rescan only known networks, but this one is not known, so skip
    }

    if (autoRescanIndex >= scannedNetworkList.size()) {
        if (scanPurpose == ScanPurpose::AutoRescanSingle && autoRescanTestChannels) {
            // we are beyond the list, but we are testing channels, so restart from beginning
            if (autoRescanTestChannelIndex < 0) {
                autoRescanTestChannelIndex = 0;
            } else {
                autoRescanTestChannelIndex = (autoRescanTestChannelIndex + 1) % autoRescanTestChannelList.size();
            }
            autoRescanTargetChannel = autoRescanTestChannelList[autoRescanTestChannelIndex];
            DBG_PRINTF_L(4,"WiFi: Auto-rescan test channel %d\n", autoRescanTargetChannel);
            autoRescanTargetBssid = "";
            scanPurpose = ScanPurpose::AutoRescanTestChannel;
            scanNetworkAsync(autoRescanTargetChannel, nullptr);
            return true;
        } else {
            // we are beyond the list, so we re-scanned everything, so we are done
            if (autoRescanSweepDidScan) {
                networkScanCount++;
                DBG_PRINTF_L(3,"WiFi: Auto-rescan complete, networkScanCount=%d\n", networkScanCount);
            }
            autoRescanActive = false;
            autoRescanIndex = 0;
            autoRescanTargetBssid = "";
            autoRescanTargetChannel = 0;
            autoRescanSweepDidScan = false;
            autoRescanKnownOnly = false;
            scanPurpose = ScanPurpose::None;
            sortNetworks();
            return false;
        }
    }

    const ScannedNetwork& target = scannedNetworkList[autoRescanIndex];
    autoRescanTargetBssid = target.bssid;
    autoRescanTargetChannel = target.channel;

    uint8_t bssid[6];
    if (!parseBssidStr(autoRescanTargetBssid, bssid) || autoRescanTargetChannel == 0) {
        // Bad entry; cannot rescan it. Keep it but mark as not detected for this sweep.
        scannedNetworkList[autoRescanIndex].scanned = false;
        scannedNetworkList[autoRescanIndex].detected = false;
        autoRescanIndex++;
        lastNetworksScanTime = millis();
        return startAutoRescanNext(autoRescanKnownOnly); // warning: recursive!
    }

    scanPurpose = ScanPurpose::AutoRescanSingle;
    DBG_PRINTF_L(3,"WiFi: Auto-scan rescan %s (%u/%u): BSSID=%s channel=%u\n",
        autoRescanKnownOnly ? "known" : "existing",
        (unsigned)(autoRescanIndex + 1),
        (unsigned)scannedNetworkList.size(),
        autoRescanTargetBssid.c_str(),
        (unsigned)autoRescanTargetChannel);
    scanNetworkAsync(autoRescanTargetChannel, bssid);
    autoRescanSweepDidScan = true;
    return true;
}

void RoamingWiFiManager::resetWiFiSta() {
    DBG_PRINTLN_L(2,"WiFi: Resetting WiFi STA mode...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY);
}

void RoamingWiFiManager::handleAutoRoaming() {
    // When connected, optionally roam to a stronger network if enabled
    if (WiFi.status() != WL_CONNECTED || !autoRoamEnabled || scanInProgress) {
        return;
    }

    const long cooldownMs = (long)(autoReconnectIntervalSec * 1000.0f);
    if (lastConnectAttemptTime != 0 && (millis() - lastConnectAttemptTime) < cooldownMs) {
        return;
    }

    const String curSsid = WiFi.SSID();
    const String curBssid = WiFi.BSSIDstr();
    const int curRssi = WiFi.RSSI();

    int bestIdx = -1;
    int bestRssi = -1000;

    for (int i = 0; i < (int)scannedNetworkList.size(); i++) {
        const ScannedNetwork& n = scannedNetworkList[i];
        if (!n.detected || !n.scanned) continue;
        if (n.bssid.equalsIgnoreCase(curBssid)) continue; // same BSSID
        if (autoRoamSameSsidOnly) {
            if (!n.ssid.equals(curSsid)) continue;
        } else {
            // Only consider known networks when roaming across SSIDs
            if (!isKnownSsid(n.ssid)) continue;
        }

        // Candidate must exceed current RSSI by delta
        const int delta = (int)autoRoamDeltaRssiDbm;
        if (n.rssi >= curRssi + delta) {
            if (n.rssi > bestRssi) {
                bestRssi = n.rssi;
                bestIdx = i;
            }
        }
    }

    if (bestIdx >= 0) {
        const ScannedNetwork& target = scannedNetworkList[bestIdx];
        DBG_PRINTF_L(2,
            "WiFi: Auto-roam: switching to stronger network: SSID=%s RSSI=%d (current %d, delta >= %.0f) BSSID=%s ch=%u\n",
            target.ssid.c_str(), target.rssi, curRssi, (double)autoRoamDeltaRssiDbm, target.bssid.c_str(), (unsigned)target.channel);
        connectToTargetNetwork(target.ssid, target.bssid, target.channel);
        lastConnectAttemptTime = millis();
    }
}

bool RoamingWiFiManager::handleStationDisconnect() {
    if (WiFi.status() != WL_CONNECTED && stationDisconnected) {
        // Restart station if station got disconnected somehow
        DBG_PRINTLN_L(1,"WiFi: Station disconnected event detected.");
        resetWiFiSta();
        stationDisconnected = false;
        return true;
    }
    return false;
}

bool RoamingWiFiManager::handleConnectionRequests() {
    // Handle user-initiated connection requests from the web UI.
    // Defer while scanning to avoid conflicting radio operations.
    if (scanInProgress) {
        return false;
    }

    if (connectionTargetRequested) {
        connectionTargetRequested = false;
        const String ssid = connectionTargetSSID;
        const String bssid = connectionTargetBSSID;
        const int channel = connectionTargetChannel;
        DBG_PRINTLN_L(2,"WiFi: Processing targeted connection request.");
        connectToTargetNetwork(ssid, bssid, channel);
        return true;
    }

    if (connectionRequested) {
        connectionRequested = false;
        DBG_PRINTLN_L(2,"WiFi: Processing strongest-network connection request.");
        connectToStrongestNetwork();
        lastConnectAttemptTime = millis();
        return true;
    }

    return false;
}

bool RoamingWiFiManager::handleAutoReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        return false;
    }

    // Handle auto-reconnect, unless when currently scanning
    if (!autoReconnectEnabled || scanInProgress) {
        return false;
    }

    long intervalMs = autoReconnectIntervalSec * 1000;
    if (lastAutoReconnectAttemptTime != 0 && (millis() - lastAutoReconnectAttemptTime < intervalMs)) {
        return false;
    }

    lastAutoReconnectAttemptTime = millis();
    autoReconnectAttemptCount++;

    DBG_PRINTF_L(2,"WiFi: Auto-reconnect attempt %u (of %u)\n", autoReconnectAttemptCount, autoReconnectResetThreshold);
    // if attempting autoreconnect too many times, reset WiFi STA
    if (autoReconnectAttemptCount > autoReconnectResetThreshold) {
        autoReconnectAttemptCount = 0;
        resetWiFiSta();
    } else {
        connectToStrongestNetwork();
    }
    lastConnectAttemptTime = millis();
    lastAutoReconnectAttemptTime = millis();
    return true;
}

bool RoamingWiFiManager::handleAutomaticScanning() {
    if (scanInProgress) {
        return false;
    }

    // Start automatic scan (full or rescan) if enabled and time elapsed
    if (autoFullScanEnabled) {
        long intervalMs = autoFullScanIntervalSec * 1000;
        if (lastAutoFullScanTime == 0 || (millis() - lastAutoFullScanTime >= intervalMs)) {
            lastAutoFullScanTime = millis();
            DBG_PRINTLN_L(2,"WiFi: Starting automatic complete network scan...");
            scanPurpose = ScanPurpose::AutoFull;
            scanNetworksFullAsync();
            return true;
        }
    }

    if (autoRescanKnownEnabled) {
        long intervalMs = autoRescanKnownIntervalSec * 1000;
        if (lastAutoRescanTime == 0 || (millis() - lastAutoRescanTime >= intervalMs)) {
            lastAutoRescanTime = millis();
            // If we have nothing yet, seed with a full scan.
            if (scannedNetworkList.empty()) {
                DBG_PRINTLN_L(2,"WiFi: Auto-rescan (existing networks) has no existing list, cannot rescan.");
            } else {
                DBG_PRINTF_L(3,"WiFi: Auto-rescan (existing networks) initiated. knownOnly=%s\n", autoRescanKnownOnlySetting ? "true" : "false");
                scanPurpose = ScanPurpose::AutoRescanSingle;
                startAutoRescanNext(autoRescanKnownOnlySetting);
                return true;
            }
        }
    }

    return false;
}

bool RoamingWiFiManager::handleAsyncScanCompletion() {
    // Handle async scan completion (used by /wifi/scan and auto-scan)
    if (!scanInProgress || WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return false;
    }

    const int scanResult = WiFi.scanComplete();
    DBG_PRINTF_L(3,"WiFi: Async scan completed. scanPurpose=%s scanResult=%d\n", toString(scanPurpose).c_str(), scanResult);
    scanInProgress = false;

    if (scanResult == WIFI_SCAN_FAILED) {
        DBG_PRINTLN_L(1,"WiFi: Asynchronous scanning failed.");
        WiFi.scanDelete();
        if (scanPurpose == ScanPurpose::AutoRescanTestChannel) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan test channel %d failed.\n", autoRescanTargetChannel);
        }
        if (scanPurpose == ScanPurpose::AutoRescanSingle) {
            if (autoRescanActive) {
                if (autoRescanIndex < scannedNetworkList.size()) {
                    scannedNetworkList[autoRescanIndex].scanned = true;
                    scannedNetworkList[autoRescanIndex].detected = false;
                    //scannedNetworkList[autoRescanIndex].rssi = -1000;
                    lastNetworksScanTime = millis();
                    lastNetworksScanType = "rescan";
                }
                autoRescanIndex++;
                startAutoRescanNext(autoRescanKnownOnly);
            }
        } else {
            // AutoFull, ManualFull
        }                
        return true;
    }

    // scanResult >= 0 : number of networks found
    if (scanPurpose == ScanPurpose::AutoRescanSingle) {
        if (scanResult == 0) {
            DBG_PRINTLN_L(4,"WiFi: Auto-rescan: no networks found in scan.");
            WiFi.scanDelete();
            if (autoRescanActive) {
                if (autoRescanIndex < scannedNetworkList.size()) {
                    scannedNetworkList[autoRescanIndex].scanned = true;
                    scannedNetworkList[autoRescanIndex].detected = false;
                    //scannedNetworkList[autoRescanIndex].rssi = -1000;
                    lastNetworksScanTime = millis();
                    lastNetworksScanType = "rescan";
                }
                // Skip this entry and continue.
                autoRescanIndex++;
                lastNetworksScanTime = millis();
                lastAutoRescanTime = millis();
                startAutoRescanNext(autoRescanKnownOnly);
            }
            return true;
        }
        if (scanResult != 1) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan: expected to find 1 network, but found %d\n. Processing first network only.", scanResult);
        }
        // use autoRescanIndex
        if (!WiFi.BSSIDstr(0).equalsIgnoreCase(autoRescanTargetBssid) || WiFi.channel(0) != autoRescanTargetChannel) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan: BSSID mismatch! Found BSSID=%s channel=%d, but requested BSSID=%s channel=%d\n", WiFi.BSSIDstr(0).c_str(), WiFi.channel(0), autoRescanTargetBssid.c_str(), autoRescanTargetChannel);
            WiFi.scanDelete();
            // Continue scanning the next entry anyway
            lastNetworksScanTime = millis();
            lastAutoRescanTime = millis();
            lastNetworksScanType = "rescan";
            if (autoRescanIndex < scannedNetworkList.size()) {
                scannedNetworkList[autoRescanIndex].scanned = true;
                scannedNetworkList[autoRescanIndex].detected = false;
                //scannedNetworkList[autoRescanIndex].rssi = -1000;
            }
            autoRescanIndex++;
            startAutoRescanNext(autoRescanKnownOnly);
            return true;
        }

        ScannedNetwork& entry = scannedNetworkList[autoRescanIndex];
        bool entryOk = true;
        if (entry.ssid != WiFi.SSID(0)) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan: SSID mismatch! Found SSID=%s, but expected SSID=%s\n", WiFi.SSID(0).c_str(), entry.ssid.c_str());
            entryOk = false;
        }
        if (entry.bssid != WiFi.BSSIDstr(0)) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan: BSSID mismatch! Found BSSID=%s, but expected BSSID=%s\n", WiFi.BSSIDstr(0).c_str(), entry.bssid.c_str());
            entryOk = false;
        }
        if (entryOk) {
            entry.ssid = WiFi.SSID(0); // must stay same
            entry.bssid = WiFi.BSSIDstr(0); // must stay same
            entry.rssi = WiFi.RSSI(0); // typically updated
            entry.channel = WiFi.channel(0); // should stay same
            entry.encryption = (WiFi.encryptionType(0) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted"; // should stay same
            entry.scanned = true;
            entry.detected = true;
            entry.known = isKnownSsid(entry.ssid);

            DBG_PRINTF_L(3,"WiFi: Auto-rescan: updated %s index %d RSSI=%d ch=%u\n", entry.bssid.c_str(), (int)autoRescanIndex, (int)entry.rssi, (unsigned)entry.channel);
        } else {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan: index %d: entry mismatch, marking as not detected.\n", (int)autoRescanIndex);
            entry.scanned = true;
            entry.detected = false;
        }
        lastNetworksScanTime = millis();
        lastAutoRescanTime = millis();
        lastNetworksScanType = "rescan";

        WiFi.scanDelete();
        autoRescanIndex++;
        startAutoRescanNext(autoRescanKnownOnly);
        return true;
    }

    if (scanPurpose == ScanPurpose::AutoRescanTestChannel) {
        if (scanResult == 0) {
            DBG_PRINTF_L(3,"WiFi: Auto-rescan test channel %d: no networks found.\n", autoRescanTargetChannel);
            WiFi.scanDelete();
            lastNetworksScanTime = millis();
            lastAutoRescanTime = millis();
            lastNetworksScanType = "rescan";
            // Continue with next channel
            startAutoRescanNext(autoRescanKnownOnly);
            return true;
        }
        DBG_PRINTF_L(3,"WiFi: Auto-rescan test channel %d: %d networks found, processing...\n", autoRescanTargetChannel, scanResult);
        // Process all found networks on this channel
        for (int i = 0; i < scanResult; i++) {
            String bssidStr = WiFi.BSSIDstr(i);
            // Find matching entry in scannedNetworkList
            bool matched = false;
            for (auto& entry : scannedNetworkList) {
                if (entry.bssid.equalsIgnoreCase(bssidStr)) {
                    // Update entry
                    entry.ssid = WiFi.SSID(i); // should stay same
                    entry.rssi = WiFi.RSSI(i); // typically updated
                    entry.channel = WiFi.channel(i); // should stay same
                    entry.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted"; // should stay same
                    entry.scanned = true;
                    entry.detected = true;
                    entry.known = isKnownSsid(entry.ssid);
                    DBG_PRINTF_L(3,"WiFi: Auto-rescan test channel: updated %s RSSI=%d ch=%u\n", entry.bssid.c_str(), (int)entry.rssi, (unsigned)entry.channel);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                DBG_PRINTF_L(3,"WiFi: Auto-rescan test channel: found unknown network %s on channel %d\n", bssidStr.c_str(), autoRescanTargetChannel);
                ScannedNetwork newEntry;
                newEntry.ssid = WiFi.SSID(i);
                newEntry.bssid = WiFi.BSSIDstr(i);
                newEntry.rssi = WiFi.RSSI(i);
                newEntry.channel = WiFi.channel(i);
                newEntry.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
                newEntry.scanned = true;
                newEntry.detected = true;
                newEntry.known = isKnownSsid(WiFi.SSID(i));
                scannedNetworkList.push_back(newEntry);
            }

        }
        WiFi.scanDelete();
        lastNetworksScanTime = millis();
        lastAutoRescanTime = millis();
        lastNetworksScanType = "rescan";
        // Continue with next channel
        scanPurpose = ScanPurpose::AutoRescanSingle;
        startAutoRescanNext(autoRescanKnownOnly);
        return true;
    }

    if (scanPurpose == ScanPurpose::AutoFull || scanPurpose == ScanPurpose::ManualFull) {
        // Full scan case
        DBG_PRINTLN_L(2,"WiFi: Full scanning completed, processing results...");
        copyScannedNetworksToList(true);
        printNetworks();
        lastNetworksScanTime = millis();
        lastAutoFullScanTime = millis();
        lastNetworksScanType = "full";
        networkScanCount++;
        scanPurpose = ScanPurpose::None;
        return true;
    }

    return false;
}

void RoamingWiFiManager::loop() {
    if (WiFi.status() == WL_CONNECTED) {
        // Reset auto-reconnect counters when connected
        lastAutoReconnectAttemptTime = 0;
        autoReconnectAttemptCount = 0;
    }

    // When connected, optionally roam to a stronger network if enabled
    handleAutoRoaming();
    if (WiFi.status() == WL_CONNECTED) {
        // Auto-roaming triggered a connection attempt; return to allow it to proceed
        if (lastConnectAttemptTime != 0 && (millis() - lastConnectAttemptTime) < 1000) {
            return;
        }
    }

    // Restart station if disconnected
    if (handleStationDisconnect()) {
        return;
    }

    // Handle user-initiated connection requests from the web UI
    if (handleConnectionRequests()) {
        return;
    }

    // Handle auto-reconnect when not connected
    if (handleAutoReconnect()) {
        return;
    }

    // Start automatic scanning if enabled and time elapsed
    if (handleAutomaticScanning()) {
        return;
    }

    // Handle async scan completion
    handleAsyncScanCompletion();
}

bool RoamingWiFiManager::useLEDIndicator = true;

void RoamingWiFiManager::setUseLEDIndicator(bool enable) {
    useLEDIndicator = enable;
}
