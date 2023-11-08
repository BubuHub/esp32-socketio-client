# Simple SocketIO client
This project implements basic SocketIO client protocol for esp-idf version 4 and above.

Example usage:
```cpp
/* http or https are supported */
SocketIoClient ws("http://127.0.0.1:8010");

/* Callback on new connection/reconnection. */
ws.setConnectCB([](SocketIoClient* ws, bool b) {
  msg_debug("Connected <%d>", b ? 1 : 0);
  if (b) {
    // ws->send("Test", 4, WS_FR_OP_TXT);
  }
});

/* Set callback for all  incomming messages */
ws.setCB([](SocketIoClient* c, const char* msg, int len, int type) {
  char buf[1024];
  strncpy(buf, msg, len);
  buf[len] = '\0';
  msg_debug("Message <%s>", buf);
});

/* Listenin to the specific event (seq-num) */
ws.on("seq-num", [](SocketIoClient* c, char* msg) {
  msg_debug("Got seq num <%s>", msg);
  c->send("ble", msg);
});

/* Start SocketIO thread */
ws.start();
```

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
