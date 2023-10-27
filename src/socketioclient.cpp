/*
 * SocketIO client implementation.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "socketioclient.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>

static char tag[] = "SIOC";

#ifdef DEBUG
#define cl_sio_debug(fmt, args...)  ESP_LOGI(tag, fmt, ## args);
#define cl_sio_info(fmt, args...)   ESP_LOGI(tag, fmt, ## args);
#define cl_sio_error(fmt, args...)  ESP_LOGE(tag, fmt, ## args);
#else
#define cl_sio_debug(fmt, args...)
#define cl_sio_info(fmt, args...)
#define cl_sio_error(fmt, args...)  ESP_LOGE(tag, fmt, ## args);
#endif

/*!
 * \brief Construct a new SocketIoClient object.
 * \param url - WebSocket url (http://, https://),
 * \param token - optional authorization token,
 * \param pingInterval_ms - ping interval in [ms],
 * \param maxBufSize - maximum RX/TX buffer size in bytes,
 * \param pr - task priority,
 * \param coreID - task CPU core.
 */
SocketIoClient::SocketIoClient(const char* url, const char* token, int pingInterval_ms, int maxBufSize, uint8_t pr, BaseType_t coreID)
{
    m_ws = new WebSocketClient(url, token, pingInterval_ms, maxBufSize, pr, coreID);

    m_ws->setConnectCB([this](WebSocketClient* ws, bool b) {
        if (!b) {
            if (this->m_ccb) this->m_ccb(this, false);
        } else {
            cl_sio_debug("send introduce");
            /* Send introduce */
            ws->send("2probe", 6);
            ws->send("5", 1);
        }
    });

    m_ws->setCB([this](WebSocketClient* c, char* payload, int length, int type) {
        char eType;

        if (length < 1) return;
        eType = (char)payload[0];

        switch (eType) {
            case SIO_IO_PING:
                payload[0] = SIO_IO_PONG;
                cl_sio_debug("get ping send pong");
                c->send(payload, length, WS_FR_OP_TXT);
                break;

            case SIO_IO_PONG:
                cl_sio_debug("get pong");
                if ((length == 6) && (!strncmp(payload, "3probe", 6))) {
                    cl_sio_debug("WS Connected :-)");
                    this->send(SIO_MSG_CONNECT, "/", 1);
                }
                break;
            case SIO_IO_MESSAGE: {
                if (length < 2) return;
                char ioType = (char)payload[1];
                char* data = &payload[2];
                int lData = length - 2;
                switch (ioType) {
                    case SIO_MSG_EVENT:
                        cl_sio_debug("get event (%d)", lData);
                        if (this->m_cb) this->m_cb(this, data, lData, ioType);
                        /* Analize and execute on callbacks */
                        if (m_on.size()) {
                            char* k = data, * x;
                            bool lev = false;
                            int len = lData;
                            /* Get first string from array */
                            while ((len > 0) && ((*k == '[') || (*k == '"') || (*k == ' '))) { if (*k == '"') lev = true; k++;len--; }
                            x = k + 1;len--;
                            while ((len > 0) && (*x != '"') && (*x != ',') && ((lev) || (*x != ' '))) { x++; len--; }
                            std::string key(k, (int)(x - k));
                            cl_sio_debug("key (%s)", key.c_str());
                            auto itr = m_on.find(key);
                            if ((len > 1) && (itr != m_on.end())) {
                                x++;len--;
                                x[len - 1] = '\0'; len--;
                                while ((len > 0) && (*x == ',') && (*x != ' ')) { x++; len--; }
                                while ((len > 0) && ((x[len - 1] == ']') || (x[len - 1] == ' '))) { x[len - 1] = '\0'; len--; }
                                if (len > 0) {
                                    for (; itr != m_on.end(); itr++) {
                                        itr->second(this, x);
                                    }
                                }
                            }
                        }
                        break;
                    case SIO_MSG_CONNECT:
                        cl_sio_debug("join (%d)", lData);
                        if (this->m_ccb) this->m_ccb(this, true);
                        return;
                    case SIO_MSG_DISCONNECT:
                    case SIO_MSG_ACK:
                    case SIO_MSG_ERROR:
                    case SIO_MSG_BINARY_EV:
                    case SIO_MSG_BINARY_ACK:
                    default:
                        cl_sio_debug("[wsIOc] Socket.IO Message Type %c (%02X) is not implemented", ioType, ioType);
                        break;
                }
            }
        }
    });
}

/*!
 * \brief Destructor.
 */
SocketIoClient::~SocketIoClient()
{
    if (m_ws) delete m_ws;
}


/*!
 * \brief Send SocketIO frame.
 * \param type - message type.
 * \param payload - pointer to message data,
 * \param length - message size in bytes,
 */
int SocketIoClient::send(char type, const char* payload, uint32_t length)
{
    uint8_t buf[2] = { SIO_IO_MESSAGE, type };
    if (length == 0) {
        length = strlen((const char*)payload);
    }
    return m_ws->send2((const char*)buf, 2, payload, length, WS_FR_OP_TXT);
}

/*!
 * \brief Send SocketIO frame.
 * \param key - message key.
 * \param val - message value,
 */
int SocketIoClient::send(const char* key, const char* val)
{
    std::string frame = "[\"" + std::string(key) + "\"," + std::string(val) + "]";
    uint8_t buf[2] = { SIO_IO_MESSAGE, SIO_MSG_EVENT };
    return m_ws->send2((const char*)buf, 2, frame.c_str(), frame.length(), WS_FR_OP_TXT);
}

