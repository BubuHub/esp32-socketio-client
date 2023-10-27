/*
 * SocketIO client implementation.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __RV_SOCKETIOCLIENT__
#define __RV_SOCKETIOCLIENT__

#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "websocketclient.h"
#include <map>
#include <string>
#include <list>

class SocketIoClient;

#define SIO_IO_OPEN         '0'    ///< Sent from the server when a new transport is opened (recheck)
#define SIO_IO_CLOSE        '1'    ///< Request the close of this transport but does not shutdown the connection itself.
#define SIO_IO_PING         '2'    ///< Sent by the client. Server should answer with a pong packet containing the same data
#define SIO_IO_PONG         '3'    ///< Sent by the server to respond to ping packets.
#define SIO_IO_MESSAGE      '4'    ///< actual message, client and server should call their callbacks with the data
#define SIO_IO_UPGRADE      '5'    ///< Before engine.io switches a transport, it tests, if server and client can communicate over this transport. If this test succeed, the client sends an upgrade packets wh
#define SIO_IO_NOOP         '6'    ///< A noop packet. Used primarily to force a poll cycle when an incoming websocket connection is received.

#define SIO_MSG_CONNECT     '0'
#define SIO_MSG_DISCONNECT  '1'
#define SIO_MSG_EVENT       '2'
#define SIO_MSG_ACK         '3'
#define SIO_MSG_ERROR       '4'
#define SIO_MSG_BINARY_EV   '5'
#define SIO_MSG_BINARY_ACK  '6'

typedef std::function<void(SocketIoClient* c, const char* msg, int len, int type)> RVSIOCB;
typedef std::function<void(SocketIoClient* c, bool connected)> RVSIOConnectedCB;
typedef std::function<void(SocketIoClient* c, char* msg)> RVSIOON;

template<typename T, typename... U>
size_t getAddress(std::function<T(U...)> f) {
    typedef T(fnType)(U...);
    fnType** fnPointer = f.template target<fnType*>();
    return (size_t)*fnPointer;
}


class SocketIoClient {
public:
    /*!
     * \brief Construct a new SocketIoClient object.
     * \param url - WebSocket url (http://, https://),
     * \param token - optional authorization token,
     * \param pingInterval_ms - ping interval in [ms],
     * \param maxBufSize - maximum RX/TX buffer size in bytes,
     * \param pr - task priority,
     * \param coreID - task CPU core.
     */
    SocketIoClient(const char* url, const char* token = NULL, int pingInterval_ms = 10000, int maxBufSize = 1024, uint8_t pr = 5, BaseType_t coreID = tskNO_AFFINITY);
    ~SocketIoClient();

    /*!
     * \brief Send SocketIO frame.
     * \param type - message type.
     * \param payload - pointer to message data,
     * \param length - message size in bytes,
     */
    int send(char type, const char* payload, uint32_t length);

    /*!
     * \brief Send SocketIO frame.
     * \param key - message key.
     * \param val - message value,
     */
    int send(const char* key, const char* val);

    /*!
     * \brief Set on message callback.
     */
    void setCB(RVSIOCB cb) { m_cb = cb; }

    /*!
     * \brief Set on connect/disconnect callback.
     */
    void setConnectCB(RVSIOConnectedCB ccb) { m_ccb = ccb; }

    void start() { m_ws->start(); }

    void on(std::string what, RVSIOON cb) {
        m_on.insert({ what, cb });
    }

    void off(std::string what) {
        std::multimap<std::string, RVSIOON>::iterator itr;
        std::list<std::multimap<std::string, RVSIOON>::iterator> l;
        for (itr = m_on.find(what); itr != m_on.end(); itr++) {
            // if (getAddress(itr->second) == getAddress(cb)) {
            l.push_back(itr);
            // }
        }
        for (auto i = l.begin(); i != l.end(); i++) {
            m_on.erase(*i);
        }
    }


private:

public:
    WebSocketClient* m_ws;
    RVSIOCB                        m_cb;
    RVSIOConnectedCB               m_ccb;
    std::multimap<std::string, RVSIOON> m_on;
};

#endif

