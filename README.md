# Simple SocketIO client
This project implements basic SocketIO client protocol for esp-iff version 4 and above.

See details in examples folder.

# Configurable parameters
Configure WiFi SSID/PASSWORD and SocketIO URL in menuconfig or by manually editing sdkconfig.defaults.

* CONFIG_EXAMPLE_WIFI_SSID
* CONFIG_EXAMPLE_WIFI_PASSWORD
* CONFIG_EXAMPLE_SIO_URL

# Building under Linux
* install PlatformIO
* enter examples/basic directory
* type in terminal:
  platformio run
  platformio upload

You can also use IDE to build this project on Linux/Windows/Mac. My fvorite ones:
* [Code](https://code.visualstudio.com/) 
* [Atom](https://atom.io/)

Enjoy :-)
