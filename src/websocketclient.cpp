/*
 * Websocket client implementation.
 *
 * Author: Rafal Vonau <rafal.vonau@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "websocketclient.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>

static char tag[] = "WSC";

#ifdef DEBUG
#define cl_ws_debug(fmt, args...)  ESP_LOGI(tag, fmt, ## args);
#define cl_ws_info(fmt, args...)   ESP_LOGI(tag, fmt, ## args);
#define cl_ws_error(fmt, args...)  ESP_LOGE(tag, fmt, ## args);
#else
#define cl_ws_debug(fmt, args...)
#define cl_ws_info(fmt, args...)
#define cl_ws_error(fmt, args...)  ESP_LOGE(tag, fmt, ## args);
#endif

#define WS_FIN  128
#define WS_MASK 128

#define directClose() esp_transport_close(m_tr)
#define directSend(data, len, timeout_ms) esp_transport_write(m_tr, data, len, timeout_ms)
#define directRecv(data, len, timeout_ms) esp_transport_read(m_tr, data, len, timeout_ms)
#define directPollRead(timeout_ms) esp_transport_poll_read(m_tr, timeout_ms)
#define directPollWrite(timeout_ms) esp_transport_poll_write(m_tr, timeout_ms)

/*!
 * \brief Construct a new WebSocketClient object.
 * \param url - WebSocket url (ws://, wss://, http://, https://),
 * \param token - optional authorization token,
 * \param pingInterval_ms - ping interval in [ms],
 * \param maxBufSize - maximum RX/TX buffer size in bytes,
 * \param pr - task priority,
 * \param coreID - task CPU core.
 */
WebSocketClient::WebSocketClient(const char* url, const char* token, int pingInterval_ms, int maxBufSize, uint8_t pr, BaseType_t coreID)
{
	m_stackSize = 10000;
	m_priority  = pr;
	m_coreId    = coreID;
	m_connected = false;
	m_ping_interval = pingInterval_ms;
	m_url = strdup(url);
	m_token = NULL;
	if (token) m_token = strdup(token);
	m_maxBuf = maxBufSize;
	m_maxBufC = maxBufSize - 1;
	/* Allocate RX/TX buffers */
	rx_buf = (char*)malloc(maxBufSize);
	tx_buf = (char*)malloc(maxBufSize);
	/* Mutex */
	vSemaphoreCreateBinary(m_lock);
	/* Default parameters */
	m_sio_v = 4;
	m_reconnectInterval = 5000;
	m_connectTimeout = 10000;
	m_writeTimeout = 10000;
	m_readTimeout = 5000;
	/* Parse url */
	parseURL();
}

/*!
 * \brief Destructor.
 */
WebSocketClient::~WebSocketClient()
{
	m_connected = false;
	if (m_url)   free(m_url);
	if (m_token) free(m_token);
	if (rx_buf)  free(rx_buf);
	if (tx_buf)  free(tx_buf);
	vSemaphoreDelete(m_lock);
	esp_transport_destroy(m_tr);
}


/*!
 * \brief Parse URL.
 */
void WebSocketClient::parseURL()
{
	char ch, * c = m_url, * port = NULL;
	m_port = 80;
	m_ssl = false;
	m_sio = false;
	m_path = "";
	m_host = "";

	/* Parse protocol */
	if (!strncmp(c, "wss://", 6)) {
		m_port = 443;
		m_ssl = true;
		c += 6;
		m_host = c;
	} else if (!strncmp(c, "ws://", 5)) {
		c += 5;
		m_host = c;
	} else if (!strncmp(c, "https://", 8)) {
		m_port = 443;
		m_ssl = true;
		m_sio = true;
		c += 8;
		m_host = c;
	} else if (!strncmp(c, "http://", 7)) {
		m_sio = true;
		c += 7;
		m_host = c;
	}
	/* Parse host */
	while (*c != '\0') {
		ch = *c;
		if (ch == ':') {
			/* Got port */
			*c = '\0';
			port = c + 1;
		} else if (ch == '/') {
			*c = '\0';
			m_path = c + 1;
			break;
		}
		c++;
	}
	if (port) {
		m_port = atoi(port);
	}
	cl_ws_debug("URL parse (host = %s, path = %s, port = %d, ssl = %d, sio = %d )", m_host, m_path, m_port, m_ssl ? 1 : 0, m_sio ? 1 : 0);

	/* Create transport */
	if (m_ssl) {
		/* Transport is SSL */
		m_tr = esp_transport_ssl_init();
	} else {
		/* Transport is TCP */
		m_tr = esp_transport_tcp_init();
	}
	esp_transport_set_default_port(m_tr, m_port);
}

/*!
 * \brief Connect to host, use rx_buf for header construction.
 * \param timeout_ms - timeout in [ms].
 */
int WebSocketClient::connect(int timeout_ms)
{
	int status, i, j, len, r, checked = 0;
	char* ch;

	m_connected = false;

	if (esp_transport_connect(m_tr, m_host, m_port, timeout_ms) < 0) {
		cl_ws_error("Unable to connect to %s:%d", m_host, m_port);
		return 0;
	}
	// m_socket.setNoDelay(false);
	{
		if (m_sio) {
			char sid[256];
			/* Ask for session ID */
			cl_ws_debug("Get session ID (/%ssocket.io/?EIO=%d&transport=polling)", m_path, m_sio_v);
			r = snprintf(rx_buf, m_maxBufC, "GET /%ssocket.io/?EIO=%d&transport=polling HTTP/1.1\r\n", m_path, m_sio_v);
			if (m_port == 80) {
				r += snprintf(rx_buf + r, m_maxBufC - r, "Host: %s\r\n", m_host);
			} else {
				r += snprintf(rx_buf + r, m_maxBufC - r, "Host: %s:%d\r\n", m_host, m_port);
			}
			if (m_token) {
				/* Add authorization token */
				r += snprintf(rx_buf + r, m_maxBufC - r, "Authorization: Token %s\r\n", m_token);
			}
			r += snprintf(rx_buf + r, m_maxBufC - r, "User-Agent: WebSocket-Client\r\nConnection: keep-alive\r\n\r\n\r\n");
			directSend(rx_buf, r, m_writeTimeout);
			/* Read back */
			for (i = 0; i < 2 || (i < m_maxBufC && rx_buf[i - 2] != '\r' && rx_buf[i - 1] != '\n'); ++i) { if (directRecv(rx_buf + i, 1, m_readTimeout) == 0) { directClose(); return 0; } }
			rx_buf[i] = 0;
			if (i == m_maxBufC) { cl_ws_error("ERROR: Got invalid status line connecting to: %s", m_url); directClose(); return -1; }
			if (sscanf(rx_buf, "HTTP/1.1 %d", &status) != 1 || status != 200) { cl_ws_error("ERROR: Got bad status connecting to %s: %s", m_url, rx_buf); directClose(); return 0; }
			// Verify response headers,
			len = m_maxBufC;
			while (1) {
				for (i = 0; i < 2 || (i < m_maxBufC && rx_buf[i - 2] != '\r' && rx_buf[i - 1] != '\n'); ++i) {
					if (directRecv(rx_buf + i, 1, m_readTimeout) == 0) {
						directClose();
						return 0;
					}
				}
				if (rx_buf[0] == '\r' && rx_buf[1] == '\n')  break;
				while ((i > 0) && ((rx_buf[i - 1] == '\r') || (rx_buf[i - 1] == '\n'))) i--;
				rx_buf[i] = '\0';
				if (!strncmp(rx_buf, "Content-Length:", 15)) {
					if (sscanf(rx_buf, "Content-Length:%d", &len) != 1) {
						len = m_maxBufC;
					} else {
						if (len > m_maxBufC) len = m_maxBufC;
					}
					cl_ws_debug("len = <%d>", len);
				}
				cl_ws_debug("header line = <%s>", rx_buf);
			}
			/* Read rest of the message */
			i = directRecv(rx_buf, len, m_readTimeout);
			if (i > 0) {
				rx_buf[i] = '\0';
				cl_ws_debug("JSON = <%s>", rx_buf);
				ch = strstr(rx_buf, "\"sid\":");
				if (ch) {
					ch += 6;
					j = 0;
					while (*ch != '\0') {
						if (*ch == ',') break;
						if (*ch == '}') break;
						if ((*ch != '"') && (*ch != ' '))
							sid[j++] = *ch;
						ch++;
					}
					sid[j] = '\0';
					cl_ws_debug("GOT sid = <%s>", sid);
				}
			}
			cl_ws_debug("/%ssocket.io/?EIO=%d&transport=websocket&sid=%s)", m_path, m_sio_v, sid);
			r = snprintf(rx_buf, m_maxBufC, "GET /%ssocket.io/?EIO=4&transport=websocket&sid=%s HTTP/1.1\r\n", m_path, sid);
		} else {
			r = snprintf(rx_buf, m_maxBufC, "GET /%s HTTP/1.1\r\n", m_path);
		}
		if (m_port == 80) {
			r += snprintf(rx_buf + r, m_maxBufC - r, "Host: %s\r\n", m_host);
		} else {
			r += snprintf(rx_buf + r, m_maxBufC - r, "Host: %s:%d\r\n", m_host, m_port);
		}
		if (m_token) {
			/* Add authorization token */
			r += snprintf(rx_buf + r, m_maxBufC - r, "Authorization: Token %s\r\n", m_token);
		}
		r += snprintf(rx_buf + r, m_maxBufC - r, "User-Agent: WebSocket-Client\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\nSec-WebSocket-Version: 13\r\n\r\n");
		directSend(rx_buf, r, m_writeTimeout);
		/* Read back */
		for (i = 0; i < 2 || (i < m_maxBufC && rx_buf[i - 2] != '\r' && rx_buf[i - 1] != '\n'); ++i) { if (directRecv(rx_buf + i, 1, m_readTimeout) == 0) { directClose(); return 0; } }
		rx_buf[i] = 0;
		if (i == m_maxBufC) { cl_ws_error("ERROR: Got invalid status line connecting to: %s", m_url); directClose(); return -1; }
		if (sscanf(rx_buf, "HTTP/1.1 %d", &status) != 1 || status != 101) { cl_ws_error("ERROR: Got bad status connecting to %s: %s", m_url, rx_buf); directClose(); return 0; }
		// Verify response headers,
		while (1) {
			for (i = 0; i < 2 || (i < m_maxBufC && rx_buf[i - 2] != '\r' && rx_buf[i - 1] != '\n'); ++i) {
				if (directRecv(rx_buf + i, 1, m_readTimeout) == 0) {
					directClose();
					return 0;
				}
			}
			if (rx_buf[0] == '\r' && rx_buf[1] == '\n')  break;
			while ((i > 0) && ((rx_buf[i - 1] == '\r') || (rx_buf[i - 1] == '\n'))) i--;
			rx_buf[i] = '\0';

			if (!strncmp(rx_buf, "Sec-WebSocket-Accept:", 21)) {
				if (strstr(rx_buf, "HSmrc0sMlYUkAGmm5OPpG2HaGWk=")) {
					checked = 1;
				} else {
					cl_ws_error("ERROR: Got invalid accept key! (line: %s)", rx_buf);
					directClose();
					return -1;
				}
			}
			cl_ws_debug("line = <%s>", rx_buf);
		}
	}
	if (!checked) {
		cl_ws_error("ERROR: Can not get accept key!");
		directClose();
		return -1;
	}
	cl_ws_debug("Connect done :-)");
	ws_ping_cnt = 0;
	ws_pong_cnt = 0;
	line_begin = 0;
	line_end = 0;
	ws_frame_size = 0;
	// m_socket.setNoDelay(true);
	m_connected = true;
	if (m_ccb) m_ccb(this, true);
	return 1;
}
//==========================================================================================

/*!
 * \brief Send WebSocket frame (use tx_buffer).
 * \param msg - pointer to message data,
 * \param size - message size in bytes,
 * \param type - message type.
 */
int WebSocketClient::send(const char* msg, uint32_t size, int type)
{
	unsigned char* response = (unsigned char*)tx_buf;
	int idx_response, res = 0, allocated = 0;
	uint32_t i;
	uint8_t idx_header;
	uint32_t length;
	uint8_t masks[4];

	int poll_write;

	if ((poll_write = directPollWrite(m_writeTimeout)) <= 0) {
		// ESP_LOGE(TAG, "Error transport_poll_write");
		return poll_write;
	}


	if (size + 15 > m_maxBuf) {
		/* Ups, internal  buffer too long - try to allocate */
		allocated = 1;
		response = (unsigned char*)malloc(size + 16);
		if (!response) return 0;
	}

	if (xSemaphoreTake(m_lock, (TickType_t)1000) == pdFALSE) return 0;

	/* Generate random mask */
	masks[0] = rand() & 0xff;
	masks[1] = rand() & 0xff;
	masks[2] = rand() & 0xff;
	masks[3] = rand() & 0xff;

	/* Construct header */
	length = size;
	/* Split the size between octets. */
	if (length <= 125) {
		idx_header = 6;
		response[0] = (WS_FIN | type);
		response[1] = (length & 0x7F) | WS_MASK;
		response[2] = masks[0];
		response[3] = masks[1];
		response[4] = masks[2];
		response[5] = masks[3];
	} else if (length >= 126 && length <= 65535) {
		/* Size between 126 and 65535 bytes. */
		idx_header = 8;
		response[0] = (WS_FIN | type);
		response[1] = 126 | WS_MASK;
		response[2] = (length >> 8) & 255;
		response[3] = length & 255;
		response[4] = masks[0];
		response[5] = masks[1];
		response[6] = masks[2];
		response[7] = masks[3];
	} else {
		/* More than 65535 bytes but limited to 4GB. */
		/* Size between 126 and 65535 bytes. */
		idx_header = 14;
		response[0] = (WS_FIN | type);
		response[1] = 127 | WS_MASK;
		response[2] = 0;
		response[3] = 0;
		response[4] = 0;
		response[5] = 0;
		response[6] = (unsigned char)((length >> 24) & 255);
		response[7] = (unsigned char)((length >> 16) & 255);
		response[8] = (unsigned char)((length >> 8) & 255);
		response[9] = (unsigned char)(length & 255);
		response[10] = masks[0];
		response[11] = masks[1];
		response[12] = masks[2];
		response[13] = masks[3];
	}

	idx_response = idx_header;

	/* Add data bytes and apply mask. */
	for (i = 0; i < length; i++) {
		response[idx_response] = msg[i] ^ masks[i % 4];
		idx_response++;
	}
	response[idx_response] = '\0';

	res = (directSend((const char*)response, idx_response, m_writeTimeout) == idx_response) ? 1 : 0;
	/* Free allocated memory */
	if (allocated) free(response);

	xSemaphoreGive(m_lock);

	return ((int)res);
}

/*!
 * \brief Send WebSocket frame (use tx_buffer).
 * \param msg0 - pointer to message0 data,
 * \param size0 - message0 size in bytes,
 * \param msg1 - pointer to message0 data,
 * \param size1 - message0 size in bytes,
 * \param type - message type.
 */
int WebSocketClient::send2(const char* msg0, uint32_t size0, const char* msg1, uint32_t size1, int type)
{
	unsigned char* response = (unsigned char*)tx_buf;
	int idx_response, res = 0, allocated = 0, poll_write;
	uint32_t i;
	uint8_t idx_header;
	uint32_t length;
	uint8_t masks[4];

	length = size0 + size1;

	if ((poll_write = directPollWrite(m_writeTimeout)) <= 0) {
		// ESP_LOGE(TAG, "Error transport_poll_write");
		return poll_write;
	}


	if (length + 15 > m_maxBuf) {
		/* Ups, internal  buffer too long - try to allocate */
		allocated = 1;
		response = (unsigned char*)malloc(length + 16);
		if (!response) return 0;
	}

	if (xSemaphoreTake(m_lock, (TickType_t)1000) == pdFALSE) return 0;

	/* Generate random mask */
	masks[0] = rand() & 0xff;
	masks[1] = rand() & 0xff;
	masks[2] = rand() & 0xff;
	masks[3] = rand() & 0xff;

	/* Construct header */

	/* Split the size between octets. */
	if (length <= 125) {
		idx_header = 6;
		response[0] = (WS_FIN | type);
		response[1] = (length & 0x7F) | WS_MASK;
		response[2] = masks[0];
		response[3] = masks[1];
		response[4] = masks[2];
		response[5] = masks[3];
	} else if (length >= 126 && length <= 65535) {
		/* Size between 126 and 65535 bytes. */
		idx_header = 8;
		response[0] = (WS_FIN | type);
		response[1] = 126 | WS_MASK;
		response[2] = (length >> 8) & 255;
		response[3] = length & 255;
		response[4] = masks[0];
		response[5] = masks[1];
		response[6] = masks[2];
		response[7] = masks[3];
	} else {
		/* More than 65535 bytes but limited to 4GB. */
		/* Size between 126 and 65535 bytes. */
		idx_header = 14;
		response[0] = (WS_FIN | type);
		response[1] = 127 | WS_MASK;
		response[2] = 0;
		response[3] = 0;
		response[4] = 0;
		response[5] = 0;
		response[6] = (unsigned char)((length >> 24) & 255);
		response[7] = (unsigned char)((length >> 16) & 255);
		response[8] = (unsigned char)((length >> 8) & 255);
		response[9] = (unsigned char)(length & 255);
		response[10] = masks[0];
		response[11] = masks[1];
		response[12] = masks[2];
		response[13] = masks[3];
	}

	idx_response = idx_header;

	/* Add data bytes and apply mask. */
	for (i = 0; i < size0; i++) {
		response[idx_response] = msg0[i] ^ masks[i % 4];
		idx_response++;
	}
	for (i = 0; i < size1; i++) {
		response[idx_response] = msg1[i] ^ masks[(size0 + i) % 4];
		idx_response++;
	}
	response[idx_response] = '\0';

	res = (directSend((const char*)response, idx_response, m_writeTimeout) == idx_response) ? 1 : 0;
	/* Free allocated memory */
	if (allocated) free(response);

	xSemaphoreGive(m_lock);

	return ((int)res);
}

/*!
 * \brief Simple pong decode (4 byte string to int).
 */
static int ws_decode_pong(char* msg, int* res)
{
	int r = 0;
	char ch = msg[0];

	/* MSB */
	if ((ch < '0') || (ch > '9')) return 0;
	r = (int)(ch - '0');

	ch = msg[1];
	if ((ch < '0') || (ch > '9')) return 0;
	r *= 10;
	r += (int)(ch - '0');

	ch = msg[2];
	if ((ch < '0') || (ch > '9')) return 0;
	r *= 10;
	r += (int)(ch - '0');

	ch = msg[3];
	if ((ch < '0') || (ch > '9')) return 0;
	r *= 10;
	r += (int)(ch - '0');

	*res = r;
	return 1;
}


/*!
 * \brief On new websocket frame.
 */
int WebSocketClient::onWsFrame()
{
	int cnt = ws_frame_size - ws_header_size;
	switch (ws_frame_type) {
		case WS_FR_OP_CONT: {
			cl_ws_debug("Got CONT frame (size = %d)", cnt);
			if (m_cb) {
				m_cb(this, ws_msg, cnt, WS_FR_OP_CONT);
			}
		} break;
		case WS_FR_OP_TXT: {
			cl_ws_debug("Got TXT frame (size = %d)", cnt);
			if (m_cb) {
				m_cb(this, ws_msg, cnt, WS_FR_OP_TXT);
			}
			/* Analize and execute on callbacks */
			if (m_on.size()) {
				char* k = ws_msg, *x;
				int len = cnt;
				bool lev = false;
				/* Get first string from array */				
				while ((len > 0) && ((*k == '[') || (*k == '"') || (*k == ' '))) { if (*k == '"') lev = true; k++;len--; }
				x = k + 1;len--;
				while ((len > 0) && (*x != '"') && (*x != ',') && ((lev) || (*x != ' '))) { x++; len--; }
				std::string key(k, (int)(x - k));
				cl_ws_debug("key (%s)", key.c_str());
				auto itr = m_on.find(key);
				if ((len > 1) && (itr != m_on.end())) {
					x++;len--;
					while ((len > 0) && ((*x == ',') || (*x == ' '))) { x++; len--; }
					while ((len > 0) && ((x[len - 1] == ']') || (x[len - 1] == ' '))) { x[len - 1] = '\0'; len--; }
					if (len > 0) {
						if (itr != m_on.end()) {
							itr->second(this, x, len);
						}
					}
				}
			}
		} break;
		case WS_FR_OP_BIN: {
			cl_ws_debug("Got BIN frame (size = %d)", cnt);
			if (m_cb) {
				m_cb(this, ws_msg, cnt, WS_FR_OP_BIN);
			}
		} break;
		case WS_FR_OP_CLOSE: {
			cl_ws_debug("Got CLOSE frame");
			return 0;
		} break;
		case WS_FR_OP_PING: {
			cl_ws_debug("Got PING frame (size = %d)", cnt);
			send(ws_msg, cnt, WS_FR_OP_PONG);
		} break;
		case WS_FR_OP_PONG: {
			if (cnt == 4) {
				if (ws_decode_pong(ws_msg, &ws_pong_cnt) == 1) {
					cl_ws_debug("Got PONG frame (size = %d, cnt = %d)", cnt, ws_pong_cnt);
					if (ws_ping_cnt != ws_pong_cnt) {
						cl_ws_debug("Bad PONG counter!");
						return 0;
					}
				}
			}
		} break;
		default: break;
	}
	return 1;
}
//===========================================================================

/*!
 * \brief Feed WebSocket frame data.
 */
int WebSocketClient::feedWsFrame()
{
	uint8_t opcode;
	int i, cur_byte, cnt;

	cl_ws_debug("WS (total = %d)", line_end);

	if (ws_frame_size == 0) {
		unsigned char* b = (unsigned char*)rx_buf;
		/* Check for frame size */
		ws_header_size = 2;
		if (line_end < 2) return 0;
		cur_byte = *b++;
		ws_is_fin = (cur_byte & 0xFF) >> 7;
		opcode = (cur_byte & 0x0F);
		ws_frame_type = opcode;
		if (cur_byte & 0x70) {
			cl_ws_debug("RSV is set while wsServer do not negotiate extensions!");
			return -1;
		}
		cur_byte = *b++;
		cnt = cur_byte & 0x7F;
		ws_is_mask = (cur_byte & 0xFF) >> 7;
		if (ws_is_mask) {
			cl_ws_debug("Frame masked! (opcode = %d)", opcode);
			return -1;
		}
		if (cnt == 126) {
			ws_header_size += 2;
			if (line_end < ws_header_size) return 0;
			cur_byte = *b++;
			cnt = (((uint64_t)cur_byte) << 8);
			cur_byte = *b++;
			cnt |= cur_byte;
		} else if (cnt == 127) {
			/* Unsupported :-( */
			cl_ws_debug("Frame too long!");
			return -1;
		}
		ws_frame_size = ws_header_size + cnt;
		cl_ws_debug("Got frame header (size = %d, opcode = %d, fin = %d, header = %d)", ws_frame_size, opcode, ws_is_fin, ws_header_size);
	}
	if (ws_frame_size > 0) {
		unsigned char* b = (unsigned char*)&rx_buf[ws_header_size];
		/* Process frame data */
		if (line_end < ws_frame_size) return 0;
		cnt = ws_frame_size - ws_header_size;
		ws_msg = (char*)b;
		/* Do something with the frame */
		if (onWsFrame() == 0) return -1;
		/* Copy next message */
		if (line_end > ws_frame_size) {
			int left = line_end - ws_frame_size;
			cl_ws_debug("Move buffer (left = %d)", left);
			for (i = 0; i < left; ++i) {
				rx_buf[i] = rx_buf[ws_frame_size + i];
			}
			line_end = left;
		} else {
			/* All data consumed :-) */
			line_end = 0;
		}
		/* Reset frame */
		ws_frame_size = 0;
		return cnt;
	}
	return 0;
}
//===========================================================================

int WebSocketClient::sendPing()
{
	if (m_connected) {
		char b[16];
		if (ws_ping_cnt != ws_pong_cnt) return 0;
		ws_ping_cnt++;
		/* Send ping */
		snprintf(b, 15, "%04d", ws_ping_cnt);
		cl_ws_debug("Send PING (%d)", ws_ping_cnt);
		send(b, 4, WS_FR_OP_PING);

	}
	return 1;
}


#define MAX_MEMORY_BUFF (1024)

/*!
 * \brief Task function (MAIN).
 */
void WebSocketClient::run()
{
	int idx;

	line_begin = 0;
	line_end = 0;
	ws_frame_size = 0;

	cl_ws_debug("Task ready");
	while (true) {
		/* ReConnect*/
		if (!m_connected) {
			if (m_ccb) m_ccb(this, false);
			while (!m_connected) {
				line_begin = 0;
				line_end = 0;
				ws_frame_size = 0;
				vTaskDelay(m_reconnectInterval / portTICK_PERIOD_MS);
				this->connect(m_connectTimeout);
			}
		}
		/* Poll */
		if (m_ping_interval) {
			idx = directPollRead(m_ping_interval);
			if (idx == 0) {
				/* Ping time */
				if (!sendPing()) {
					cl_ws_error("No PONG received! - remove socket");
					directClose();
					m_connected = false;
				}
				continue;
			} else if (idx < 0) {
				cl_ws_debug("Remove socket");
				directClose();
				m_connected = false;
				continue;
			}
		}
		/* Receive */
		idx = directRecv(&rx_buf[line_end], (MAX_MEMORY_BUFF - line_end), -1);
		/* Parse */
		if (idx <= 0) {
			cl_ws_debug("Remove socket");
			directClose();
			m_connected = false;
			continue;
		}
		line_end += idx;
		/* Parse websocket message */
		while (idx > 0) {
			idx = feedWsFrame();
		}
		if (idx < 0) {
			cl_ws_debug("Remove socket");
			directClose();
			m_connected = false;
			continue;
		}
	}
}

static void WebSocketClientRunTask(void* arg) 
{
	WebSocketClient* p = (WebSocketClient*) arg;
	p->run();
	p->stop();
}

/*!
 * \brief Stop WebSocketClient task.
 */
void WebSocketClient::stop() 
{
	if (m_handle == nullptr) return;
	::vTaskDelete(m_handle);
	m_handle = NULL;
}

/*!
 * \brief Start WebSocketClient task.
 */
void WebSocketClient::start() 
{
	::xTaskCreatePinnedToCore(&WebSocketClientRunTask, "WebSocketClient", m_stackSize, this, m_priority, &m_handle, m_coreId);
}

