# esp32-wifi-manager
Wifi connectivity manager aimed for iotroam networks at university, supporting active roaming. Useful for static and mobile robots

## features

- display mac address to serial so that you can register it at iotroam.nl
- connect to iotroam with given password, using 5 GHz by default
- active roaming: periodically scan networks asynchronously, connecting to a significantly stronger hotspot if available
- http endpoint at /wifi with status information and some other generic data
- designed for easy integration with other ESP32-C5 projects
- control LED on ESP32-C5 (official devkit or waveshare) to show wifi status
