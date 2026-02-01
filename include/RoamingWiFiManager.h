#pragma once
//#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

class NetworkCredentials {
public:
    String ssid;
    String password;
};

class ScannedNetwork {
public:
    String ssid;
    int32_t rssi;
    String bssid;
    uint8_t channel;
    String encryption;
    bool scanned;
    bool detected;
    bool known;
public:
    bool isEmpty() {
        return ssid.isEmpty();
    }
};

class RoamingWiFiManager {
    public:
        // Reads MAC address from wifi chip and prints it to Serial. Also sets mode to STA and 5GHz.
        //static void printMAC();

        // Returns MAC address of the wifi chip as a string.
        static String getMAC();

        // Returns the IP address of the currently connected network as a string. If not connected, returns "".
        String getConnectedIp();

        // By default the manager controls the LED. Disable it to control it yourself.
        static void setUseLEDIndicator(bool enable);

        // Constructor. serverPort: port number for the web server (default 80).
        RoamingWiFiManager(int serverPort=80);

        // Initializes the RoamingWiFiManager with known networks, optional BSSID alias URL, and optional admin credentials.
        void init(std::vector<NetworkCredentials> knownCredentials, std::pair<String, String> adminCredentials = {"", ""}, String bssidAliasesUrl = "");

        bool isConnected() {
            return (WiFi.status() == WL_CONNECTED);
        }

        // Main loop function to be called regularly, to handle async scanning, auto-reconnects and such
        void loop();

        // Web server instance
        AsyncWebServer server;

        static int debugLevel; // Debug level: 0=none, 1-5=increasing verbosity (persisted)
    private:
        enum class ScanPurpose {
            None,
            ManualFull, // full scan, manually triggered by e.g., /wifi/scan
            AutoFull,   // full scan, automatically triggered
            AutoRescanSingle, // rescan of a single network, automatically triggered
            AutoRescanTestChannel, // test scan of a single channel, automatically triggered
        };

        // converts ScanPurpose to string
        static String toString(ScanPurpose purpose);

        // Static helper methods
        static bool parseBssid(const String& bssidStr, uint8_t bssid[6]);
        static bool isDfsChannel(uint8_t channel); // Check if a channel is a DFS channel

        // Helper methods for splitting large functions
        void loadScanSettings();
        void loadStatusSettings();
        void loadReconnectSettings();
        void loadRoamSettings();
        void loadDebugLevel();
        void loadNetworkInfo();
        
        void handleAutoRoaming();
        bool handleStationDisconnect();
        bool handleConnectionRequests();
        bool handleAutoReconnect();
        bool handleAutomaticScanning();
        bool handleAsyncScanCompletion();
        
        void setupStatusEndpoints();
        void setupScanEndpoints();
        void setupConnectionEndpoints();
        void setupSettingsEndpoints();
        void setupMainEndpoint();

        // timestamps are all in ms
        std::vector<NetworkCredentials> knownNetworks; // known networks to try connecting to
        std::vector<ScannedNetwork> scannedNetworkList; // scanned networks from last scan
        std::vector<String> _clientIpAddresses; // list of assigned IP addresses, to help finding the unknown client IP for a specific network

        String _adminUser;
        String _adminPassword;

        // timestamps are in ms
        unsigned long wifiConnectedTime = 0;
        // Deprecated: use lastNetworksScanTime and lastNetworksScanType instead
        // unsigned long lastScanTime = 0;
        unsigned long lastAutoFullScanTime = 0;
        unsigned long lastAutoRescanTime = 0;
        unsigned long lastConnectAttemptTime = 0; // time of last (re)connection attempt
        unsigned long lastNetworksScanTime = 0; // when the visible networks list was last updated from a scan (ms)
        unsigned long lastAutoReconnectAttemptTime = 0; // last auto-reconnect attempt time (ms)

        static bool useLEDIndicator; // if true, use built-in LED to indicate status
        uint32_t networkScanCount = 0; // number of completed full scans (and completed rescan sweeps) since boot

        // How the current scannedNetworkList was produced.
        // Values used by /wifi/networks: "full", "rescan", "fastReconnect" (or "none").
        String lastNetworksScanType = "none";

        uint32_t autoReconnectAttemptCount = 0; // counts reconnect attempts while disconnected
        uint32_t autoReconnectResetThreshold = 3; // if attempts exceed this, reset WiFi STA mode

        // Automatic scan settings (persisted)
        bool autoFullScanEnabled = false; // periodic full scan (default disabled)
        float autoFullScanIntervalSec = 10.0f;
        bool autoRescanKnownEnabled = true; // periodic rescan of known/existing networks (default enabled)
        float autoRescanKnownIntervalSec = 1.0f;
        bool autoRescanKnownOnlySetting = true; // if true, automatic rescan targets known networks only; otherwise all existing networks

        // Scan time settings (persisted)
        uint32_t scanTimeNonDfsMs = 50; // max scan time per channel for non-DFS channels (ms)
        uint32_t scanTimeDfsMs = 200;    // max scan time per channel for DFS channels (ms)

        bool scanInProgress = false;
        ScanPurpose scanPurpose = ScanPurpose::None;
        bool autoRescanActive = false;
        size_t autoRescanIndex = 0;
        String autoRescanTargetBssid = "";
        uint8_t autoRescanTargetChannel = 0;
        bool autoRescanSweepDidScan = false; // true if this rescan sweep actually started at least one scan
        bool autoRescanKnownOnly = false; // true if the current rescan sweep only targets known networks; for now managed by startAutoRescanNext(knownOnly)
        bool autoRescanTestChannels = true; // if true, after full auto-rescan sweep, also test one channel
        bool autoRescanSkipNotDetected = true; // if true, skip non-detected networks during rescan to avoid wasting resources
        float autoRescanWaitIntervalSec = 10.0f; // wait time between consecutive series of scans during auto-rescan (seconds), persisted
        unsigned long lastAutoRescanSingleScanTime = 0; // timestamp when last single network scan completed (ms)
        int autoRescanTestChannelIndex = -1; // last channel tested if autoRescanTestChannels is true
        std::vector<int> autoRescanTestChannelList = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177}; // list of channels to test if autoRescanTestChannels is true
        bool connectionRequested = false; // Flag for connection request from web interface
        bool connectionTargetRequested = false; // Flag for targeted connection request from web interface
        String connectionTargetSSID = "";
        String connectionTargetBSSID = "";
        int connectionTargetChannel = 0;
        float statusRefreshIntervalSec = 0.5f; // Auto-refresh status interval (seconds), persisted
        bool statusAutoRefreshEnabled = true; // Enable/disable status auto-refresh (UI setting), persisted
        bool autoReconnectEnabled = true; // If disconnected, auto-reconnect to strongest known network
        float autoReconnectIntervalSec = 5.0f; // Auto-reconnect interval (seconds), persisted
        // Auto-roam settings: optionally switch to a stronger network while connected
        bool autoRoamEnabled = true; // Enable auto-connect to stronger network
        float autoRoamDeltaRssiDbm = 10.0f; // Minimum RSSI delta (dBm) to trigger roam
        bool autoRoamSameSsidOnly = true; // If true, only roam within the same SSID
        Preferences wifiPrefs;            // NVS preferences for persistence
        String savedBSSID = "";          // Last successfully connected BSSID (persisted)
        String savedSSID = "";           // Last successfully connected SSID (persisted)
        int savedChannel = 0;             // Last channel used
        bool lastQuickReconnectSuccess = false; // Persisted: last fast reconnect attempt succeeded
        bool stationDisconnected = false; // if true then it must be restarted somehow

        // Optional: URL to a JSON document mapping BSSID -> alias for display in the web UI.
        String bssidAliasesUrl = "";

        // Like, wifi connected/disconnected etc
        void handleWiFiEvent(WiFiEvent_t event, arduino_event_info_t info);
        // Helper to send a unified 401 Unauthorized response with WWW-Authenticate header
        void sendUnauthorized(AsyncWebServerRequest *request, const char* message);
        bool checkHttpAuth(AsyncWebServerRequest *request); // check HTTP Basic Auth
        bool connectDirectSaved(); // attempt connect using saved SSID/BSSID/channel without scanning
        bool loadPersistedSettings(); // returns true if successful, false if not.
        void persistConnectedNetwork(); // save current connection (SSID/BSSID/channel) to NVS
        // Copies scanned networks from WiFi to scannedNetworkList.
        // If keepExisting is true: keep list entries, update/append scanned ones, and mark missing as not detected.
        void copyScannedNetworksToList(bool keepExisting);
        void sortNetworks(); // first all known networks (sorted by RSSI), then unknown networks (sorted by RSSI)
        void printNetworks();
        ScannedNetwork findBestNetworkVar();
        void connectToStrongestNetwork(); // strongest in scannedNetworks
        void connectToTargetNetwork(const String& ssid, const String& bssid, int channel);
        void resetWiFiSta();
        void setupWebServer(); // sets up web server routes
        String getWiFiStatus();

        // full scan
        void scanNetworksFullAsync();

        // async rescan of a single network based on known channel and bssid, usually taken from scannedNetworkList[autoRescanIndex]
        // if bssid is empty, scan all BSSIDs on that channel
        void scanNetworkAsync(uint8_t channel, const uint8_t* bssid);

        // Rescan existing networks only.
        // Returns true if at least one network was found to rescan, false if scan in progress or no network found to scan.
        // knownOnly: if true, only networks in knownNetworks are considered for rescanning. This speeds up quick scanning of interesting channels.
        bool startAutoRescanNext(bool knownOnly);

        String getPasswordOfNetwork(String ssid); // returns empty string if ssid not found in list of known networks
        JsonDocument getScannedNetworksAsJsonDocument();

        bool isKnownSsid(const String& ssid);

        // JSON helpers
        void sendJsonError(AsyncWebServerRequest* request, int code, const char* message);
        bool tryParseJson(const String& body, JsonDocument& doc, AsyncWebServerRequest* request);
};
