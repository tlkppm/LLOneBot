#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include "../core/Logger.h"

namespace LCHBOT {

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    WebSocketClient() : running_(false), socket_(INVALID_SOCKET) {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }
    
    ~WebSocketClient() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool connect(const std::string& host, uint16_t port, const std::string& path = "/") {
        host_ = host;
        port_ = port;
        path_ = path;
        
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            if (on_error_) on_error_("Failed to create socket");
            return false;
        }
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(host.c_str());
            if (he == nullptr) {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
                if (on_error_) on_error_("Failed to resolve host: " + host);
                return false;
            }
            addr.sin_addr = *((struct in_addr*)he->h_addr);
        }
        
        if (::connect(socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            if (on_error_) on_error_("Failed to connect to " + host + ":" + std::to_string(port));
            return false;
        }
        
        if (!performHandshake()) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            if (on_error_) on_error_("WebSocket handshake failed");
            return false;
        }
        
        running_ = true;
        recv_thread_ = std::thread(&WebSocketClient::recvLoop, this);
        
        if (on_connect_) on_connect_();
        
        return true;
    }
    
    void disconnect() {
        running_ = false;
        
        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            if (socket_ != INVALID_SOCKET) {
                std::vector<uint8_t> close_frame = encodeFrame("", 0x08, true);
                ::send(socket_, (const char*)close_frame.data(), (int)close_frame.size(), 0);
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
            }
        }
        
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }
    
    void send(const std::string& message) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (socket_ == INVALID_SOCKET) {
            LOG_WARN("[WebSocket] Send failed: socket invalid");
            return;
        }
        
        std::vector<uint8_t> frame = encodeFrame(message, 0x01, true);
        int sent = ::send(socket_, (const char*)frame.data(), (int)frame.size(), 0);
        if (sent < 0) {
            LOG_ERROR("[WebSocket] Send error: " + std::to_string(sent));
        }
    }
    
    bool isConnected() const { return running_ && socket_ != INVALID_SOCKET; }
    
    void setMessageCallback(MessageCallback callback) { on_message_ = std::move(callback); }
    void setConnectCallback(ConnectCallback callback) { on_connect_ = std::move(callback); }
    void setDisconnectCallback(DisconnectCallback callback) { on_disconnect_ = std::move(callback); }
    void setErrorCallback(ErrorCallback callback) { on_error_ = std::move(callback); }
    
private:
    bool performHandshake() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        uint8_t key_bytes[16];
        for (int i = 0; i < 16; ++i) {
            key_bytes[i] = static_cast<uint8_t>(dis(gen));
        }
        
        std::string key = base64Encode(key_bytes, 16);
        
        std::ostringstream request;
        request << "GET " << path_ << " HTTP/1.1\r\n";
        request << "Host: " << host_ << ":" << port_ << "\r\n";
        request << "Upgrade: websocket\r\n";
        request << "Connection: Upgrade\r\n";
        request << "Sec-WebSocket-Key: " << key << "\r\n";
        request << "Sec-WebSocket-Version: 13\r\n";
        request << "\r\n";
        
        std::string req = request.str();
        if (::send(socket_, req.c_str(), (int)req.size(), 0) == SOCKET_ERROR) {
            return false;
        }
        
        char buffer[4096];
        int received = recv(socket_, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            return false;
        }
        
        buffer[received] = '\0';
        std::string response(buffer);
        
        if (response.find("101") == std::string::npos) {
            return false;
        }
        
        if (response.find("Upgrade") == std::string::npos && 
            response.find("upgrade") == std::string::npos) {
            return false;
        }
        
        return true;
    }
    
    void recvLoop() {
        std::vector<uint8_t> buffer(1024 * 1024);
        std::vector<uint8_t> pending_data;
        
        while (running_) {
            int received = recv(socket_, (char*)buffer.data(), (int)buffer.size(), 0);
            
            if (received <= 0) {
                LOG_WARN("[WebSocket] recv returned " + std::to_string(received) + ", errno=" + std::to_string(WSAGetLastError()));
                break;
            }
            
            pending_data.insert(pending_data.end(), buffer.begin(), buffer.begin() + received);
            
            size_t offset = 0;
            while (offset < pending_data.size()) {
                auto [opcode, payload, consumed] = decodeFrame(pending_data.data() + offset, pending_data.size() - offset);
                if (consumed == 0) break;
                
                offset += consumed;
                
                if (opcode == 0x08) {
                    LOG_WARN("[WebSocket] Received close frame from server");
                    goto disconnect_label;
                } else if (opcode == 0x09) {
                    std::vector<uint8_t> pong_frame = encodeFrame(payload, 0x0A, true);
                    {
                        std::lock_guard<std::mutex> lock(send_mutex_);
                        if (socket_ != INVALID_SOCKET) {
                            ::send(socket_, (const char*)pong_frame.data(), (int)pong_frame.size(), 0);
                        }
                    }
                } else if (opcode == 0x01 || opcode == 0x02) {
                    if (on_message_) {
                        std::string msg_copy = payload;
                        std::thread([this, msg_copy]() {
                            on_message_(msg_copy);
                        }).detach();
                    }
                }
            }
            
            if (offset > 0) {
                pending_data.erase(pending_data.begin(), pending_data.begin() + offset);
            }
        }
        
    disconnect_label:
        if (running_) {
            running_ = false;
            if (on_disconnect_) on_disconnect_();
        }
    }
    
    std::vector<uint8_t> encodeFrame(const std::string& payload, uint8_t opcode, bool mask) {
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | opcode);
        
        uint8_t mask_bit = mask ? 0x80 : 0x00;
        
        if (payload.size() < 126) {
            frame.push_back(mask_bit | static_cast<uint8_t>(payload.size()));
        } else if (payload.size() < 65536) {
            frame.push_back(mask_bit | 126);
            frame.push_back((payload.size() >> 8) & 0xFF);
            frame.push_back(payload.size() & 0xFF);
        } else {
            frame.push_back(mask_bit | 127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((payload.size() >> (i * 8)) & 0xFF);
            }
        }
        
        if (mask) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);
            
            uint8_t mask_key[4];
            for (int i = 0; i < 4; ++i) {
                mask_key[i] = static_cast<uint8_t>(dis(gen));
                frame.push_back(mask_key[i]);
            }
            
            for (size_t i = 0; i < payload.size(); ++i) {
                frame.push_back(payload[i] ^ mask_key[i % 4]);
            }
        } else {
            frame.insert(frame.end(), payload.begin(), payload.end());
        }
        
        return frame;
    }
    
    std::tuple<uint8_t, std::string, size_t> decodeFrame(const uint8_t* data, size_t len) {
        if (len < 2) return {0, "", 0};
        
        uint8_t opcode = data[0] & 0x0F;
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;
        size_t offset = 2;
        
        if (payload_len == 126) {
            if (len < 4) return {0, "", 0};
            payload_len = (data[2] << 8) | data[3];
            offset = 4;
        } else if (payload_len == 127) {
            if (len < 10) return {0, "", 0};
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            offset = 10;
        }
        
        uint8_t mask[4] = {0};
        if (masked) {
            if (len < offset + 4) return {0, "", 0};
            std::memcpy(mask, data + offset, 4);
            offset += 4;
        }
        
        if (len < offset + payload_len) return {0, "", 0};
        
        std::string payload(payload_len, '\0');
        for (size_t i = 0; i < payload_len; ++i) {
            payload[i] = masked ? (data[offset + i] ^ mask[i % 4]) : data[offset + i];
        }
        
        return {opcode, payload, offset + payload_len};
    }
    
    std::string base64Encode(const uint8_t* data, size_t len) {
        static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        for (size_t i = 0; i < len; i += 3) {
            uint32_t triple = (data[i] << 16);
            if (i + 1 < len) triple |= (data[i + 1] << 8);
            if (i + 2 < len) triple |= data[i + 2];
            
            result += base64_chars[(triple >> 18) & 0x3F];
            result += base64_chars[(triple >> 12) & 0x3F];
            result += (i + 1 < len) ? base64_chars[(triple >> 6) & 0x3F] : '=';
            result += (i + 2 < len) ? base64_chars[triple & 0x3F] : '=';
        }
        return result;
    }
    
    std::atomic<bool> running_;
    SOCKET socket_;
    std::thread recv_thread_;
    mutable std::mutex send_mutex_;
    
    std::string host_;
    uint16_t port_;
    std::string path_;
    
    MessageCallback on_message_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
    ErrorCallback on_error_;
};

}
