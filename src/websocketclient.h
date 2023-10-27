/*
 * Websocket client implementation.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __RV_WEBSOCKETCLIENT__
#define __RV_WEBSOCKETCLIENT__

#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_transport.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ssl.h>
#include <map>
#include <string>

#define WS_FR_OP_CONT  (0)
#define WS_FR_OP_TXT   (1)
#define WS_FR_OP_BIN   (2)
#define WS_FR_OP_CLOSE (8)
#define WS_FR_OP_PING  (0x9)
#define WS_FR_OP_PONG  (0xA)

class WebSocketClient;

typedef std::function<void(WebSocketClient* c, char* msg, int len, int type)> RVWebSocketCB;
typedef std::function<void(WebSocketClient* c, bool connected)> RVWebSocketConnectedCB;
typedef std::function<void(WebSocketClient* c, char* msg, int len)> RVWebSocketON;

class WebSocketClient {
public:
    /*!
     * \brief Construct a new WebSocketClient object.
     * \param url - WebSocket url (ws://, wss://, http://, https://),
     * \param token - optional authorization token,
     * \param pingInterval_ms - ping interval in [ms],
     * \param maxBufSize - maximum RX/TX buffer size in bytes,
     * \param pr - task priority,
     * \param coreID - task CPU core.
     */
    WebSocketClient(const char* url, const char* token = NULL, int pingInterval_ms = 10000, int maxBufSize = 1024, uint8_t pr = 5, BaseType_t coreID = tskNO_AFFINITY);
    ~WebSocketClient();

    /*!
     * \brief Send WebSocket frame (use tx_buffer).
     * \param msg - pointer to message data,
     * \param size - message size in bytes,
     * \param type - message type.
     */
    int send(const char* msg, uint32_t size, int type = WS_FR_OP_TXT);

    /*!
     * \brief Send WebSocket frame (use tx_buffer).
     * \param msg0 - pointer to message0 data,
     * \param size0 - message0 size in bytes,
     * \param msg1 - pointer to message0 data,
     * \param size1 - message0 size in bytes,
     * \param type - message type.
     */
    int send2(const char* msg0, uint32_t size0, const char* msg1, uint32_t size1, int type = WS_FR_OP_TXT);


    /*!
     * \brief Set on message callback.
     */
    void setCB(RVWebSocketCB cb) { m_cb = cb; }

    /*!
     * \brief Set on connect/disconnect callback.
     */
    void setConnectCB(RVWebSocketConnectedCB ccb) { m_ccb = ccb; }

    /*!
     * \brief Start WebSocketClient task.
     */
    void start();

    /*!
     * \brief Stop WebSocketClient task.
     */
    void stop();

    /*!
     * \brief Task function (MAIN).
     */
    void run();


    /* Parameters */
    void setPingInterval(int ms) { m_ping_interval = ms; }
    int  getPingInterval() const { return m_ping_interval; }

    void setReconnectInterval(int ms) { m_reconnectInterval = ms; }
    int  getReconnectInterval() const { return m_reconnectInterval; }

    void setConnectTimeout(int ms) { m_connectTimeout = ms; }
    int  getConnectTimeout() const { return m_connectTimeout; }

    void setWriteTimeout(int ms) { m_writeTimeout = ms; }
    int  getWriteTimeout() const { return m_writeTimeout; }

    void setReadTimeout(int ms) { m_readTimeout = ms; }
    int  getReadTimeout() const { return m_readTimeout; }

    bool isConnected() const { return m_connected; }


    void on(std::string what, RVWebSocketON cb) {
        m_on.insert({ what, cb });
    }

    void off(std::string what) {
        m_on.erase(what);
    }

private:
    /*!
     * \brief Connect to host, use rx_buf for header construction.
     * \param timeout_ms - timeout in [ms].
     */
    int connect(int timeout_ms = 10000);
    void parseURL();
    int feedWsFrame();
    int onWsFrame();
    int sendPing();

public:
    esp_transport_handle_t m_tr; /* Transport */
    /* Parameters from link */
    char* m_url;
    char* m_token;
    int               m_port;
    bool              m_ssl;
    bool              m_sio;
    int               m_sio_v;
    const char* m_path;
    const char* m_host;
    /* Parameters */
    int               m_reconnectInterval;
    int               m_connectTimeout;
    int               m_writeTimeout;
    int               m_readTimeout;
    /* RX/TX buffers */
    int               m_maxBuf;
    int               m_maxBufC;
    char             *rx_buf;
    int               line_begin;           /*!< next line start pointer             */
    int               line_pos;             /*!< current position in buffer          */
    int               line_end;             /*!< End of arrived data in the buffer   */
    char             *tx_buf;
    /* Current frame info */
    uint8_t           ws_frame_type;        /*!< Websocket frame type                */
    uint8_t           ws_is_fin;            /*!< Websocket frame is final            */
    uint8_t           ws_is_mask;           /*!< Websocket frame is mask             */
    uint8_t           ws_is_ctrl;           /*!< Websocket frame is control          */
    int               ws_header_size;       /*!< Websocket frame header size         */
    int               ws_frame_size;        /*!< Websocket frame size                */
    char             *ws_msg;               /*!< Websocket frame message pointer     */
    /* Ping/Pong */
    int               m_ping_interval;
    int               ws_ping_cnt;          /*!< Websocket ping counter              */
    int               ws_pong_cnt;          /*!< Websocket pong counter              */
    /* misc */
    bool              m_connected;
    RVWebSocketCB     m_cb;
    RVWebSocketConnectedCB m_ccb;
    std::map<std::string, RVWebSocketON> m_on;
    SemaphoreHandle_t m_lock;
    /* task */
    xTaskHandle       m_handle;
    uint16_t          m_stackSize;
    uint8_t           m_priority;
    BaseType_t        m_coreId;
};

#endif

