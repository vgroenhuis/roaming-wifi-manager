# roaming-wifi-manager

Roaming wifi connectivity manager is aimed for mesh networks such as iotroam at universities.
Active roaming is supported, making it useful for mobile robots and also for static setups which are carried around inside buildings.
The manager ensures that the device keeps connected to the strongest access point with the same (or different) SSID, and continuously scans for new access points across the available channels.
An ESP32 device supporting 5 GHz is required, such as the ESP32-C5 family. The 2.4 GHz band is generally heavily congested in buildings with many access points and therefore not reliable.
An extensive web interface is available to control scanning and auto-reconnecting options and show the signal strength history of the relevant access points.

## features

- extensive admin panel
- active roaming: periodically scan networks asynchronously, connecting to a significantly stronger hotspot if available
- designed for easy integration with other ESP32-C5 projects
- control RGB LED on ESP32-C5 devkit to show wifi status

## LED color
- white: boot
- orange: scanning networks (synchronously)
- orange flashing: connecting to best network
- blue: processing scanned networks
- green: connected
- magenta: re-scanning channel
- red: disconnected

# supported (recommended) boards
- ESP32-C5-DevKitC-1 (Espressif)
- ESP32-C5-WIFI6-KIT-N16R8 (WaveShare)

## todo
- control which channels are (re-)scanned, which are active and which passive
