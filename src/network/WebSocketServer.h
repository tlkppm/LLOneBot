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
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

namespace LCHBOT {

class WebSocketServer {
public:
    using MessageCallback = std::function<void(int, const std::string&)>;
    using ConnectCallback = std::function<void(int)>;
    using DisconnectCallback = std::function<void(int)>;
    
    WebSocketServer() : running_(false), server_socket_(INVALID_SOCKET), next_client_id_(1) {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }
    
    ~WebSocketServer() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool start(const std::string& host, uint16_t port) {
        server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET) {
            return false;
        }
        
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
        if (bind(server_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(server_socket_);
            return false;
        }
        
        if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server_socket_);
            return false;
        }
        
        running_ = true;
        accept_thread_ = std::thread(&WebSocketServer::acceptLoop, this);
        
        return true;
    }
    
    void stop() {
        running_ = false;
        
        if (server_socket_ != INVALID_SOCKET) {
            closesocket(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto& [id, client] : clients_) {
                closesocket(client.socket);
            }
            clients_.clear();
        }
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }
    
    void send(int client_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end()) return;
        
        std::vector<uint8_t> frame = encodeFrame(message, 0x01);
        ::send(it->second.socket, (const char*)frame.data(), (int)frame.size(), 0);
    }
    
    void broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::vector<uint8_t> frame = encodeFrame(message, 0x01);
        for (auto& [id, client] : clients_) {
            ::send(client.socket, (const char*)frame.data(), (int)frame.size(), 0);
        }
    }
    
    void setMessageCallback(MessageCallback callback) { on_message_ = std::move(callback); }
    void setConnectCallback(ConnectCallback callback) { on_connect_ = std::move(callback); }
    void setDisconnectCallback(DisconnectCallback callback) { on_disconnect_ = std::move(callback); }
    
    bool isRunning() const { return running_; }
    
private:
    struct Client {
        SOCKET socket;
        std::thread recv_thread;
        bool handshake_complete = false;
    };
    
    void acceptLoop() {
        while (running_) {
            sockaddr_in client_addr{};
            int addr_len = sizeof(client_addr);
            SOCKET client_socket = accept(server_socket_, (sockaddr*)&client_addr, &addr_len);
            
            if (client_socket == INVALID_SOCKET) {
                if (!running_) break;
                continue;
            }
            
            int client_id = next_client_id_++;
            
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_[client_id].socket = client_socket;
                clients_[client_id].recv_thread = std::thread(&WebSocketServer::clientLoop, this, client_id);
            }
        }
    }
    
    void clientLoop(int client_id) {
        SOCKET socket;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            socket = clients_[client_id].socket;
        }
        
        if (!performHandshake(client_id, socket)) {
            removeClient(client_id);
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[client_id].handshake_complete = true;
        }
        
        if (on_connect_) on_connect_(client_id);
        
        std::vector<uint8_t> buffer(65536);
        std::string message_buffer;
        
        while (running_) {
            int received = recv(socket, (char*)buffer.data(), (int)buffer.size(), 0);
            
            if (received <= 0) {
                break;
            }
            
            size_t offset = 0;
            while (offset < (size_t)received) {
                auto [opcode, payload, consumed] = decodeFrame(buffer.data() + offset, received - offset);
                if (consumed == 0) break;
                
                offset += consumed;
                
                if (opcode == 0x08) {
                    std::vector<uint8_t> close_frame = encodeFrame("", 0x08);
                    ::send(socket, (const char*)close_frame.data(), (int)close_frame.size(), 0);
                    goto disconnect;
                } else if (opcode == 0x09) {
                    std::vector<uint8_t> pong_frame = encodeFrame(payload, 0x0A);
                    ::send(socket, (const char*)pong_frame.data(), (int)pong_frame.size(), 0);
                } else if (opcode == 0x01 || opcode == 0x02) {
                    if (on_message_) on_message_(client_id, payload);
                }
            }
        }
        
    disconnect:
        if (on_disconnect_) on_disconnect_(client_id);
        removeClient(client_id);
    }
    
    bool performHandshake(int client_id, SOCKET socket) {
        char buffer[8192];
        int received = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) return false;
        
        buffer[received] = '\0';
        std::string request(buffer);
        
        if (request.find("GET") == std::string::npos) {
            return false;
        }
        
        std::string key;
        std::istringstream iss(request);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.size() > 0 && line.back() == '\r') line.pop_back();
            
            std::string lower_line = line;
            for (auto& c : lower_line) c = std::tolower(c);
            
            if (lower_line.find("sec-websocket-key:") != std::string::npos) {
                size_t pos = line.find(':');
                key = line.substr(pos + 1);
                while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
                while (!key.empty() && (key.back() == '\r' || key.back() == '\n' || key.back() == ' ')) key.pop_back();
                break;
            }
        }
        
        if (key.empty()) return false;
        
        std::string accept_key = computeAcceptKey(key);
        
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n";
        response << "Upgrade: websocket\r\n";
        response << "Connection: Upgrade\r\n";
        response << "Sec-WebSocket-Accept: " << accept_key << "\r\n";
        response << "\r\n";
        
        std::string resp = response.str();
        ::send(socket, resp.c_str(), (int)resp.size(), 0);
        
        return true;
    }
    
    std::string computeAcceptKey(const std::string& key) {
        std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        
        uint32_t h0 = 0x67452301;
        uint32_t h1 = 0xEFCDAB89;
        uint32_t h2 = 0x98BADCFE;
        uint32_t h3 = 0x10325476;
        uint32_t h4 = 0xC3D2E1F0;
        
        std::vector<uint8_t> msg(combined.begin(), combined.end());
        uint64_t original_len = msg.size() * 8;
        
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) msg.push_back(0);
        
        for (int i = 7; i >= 0; --i) {
            msg.push_back((original_len >> (i * 8)) & 0xFF);
        }
        
        auto leftRotate = [](uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); };
        
        for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i) {
                w[i] = (msg[chunk + i * 4] << 24) | (msg[chunk + i * 4 + 1] << 16) |
                       (msg[chunk + i * 4 + 2] << 8) | msg[chunk + i * 4 + 3];
            }
            for (int i = 16; i < 80; ++i) {
                w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }
            
            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            
            for (int i = 0; i < 80; ++i) {
                uint32_t f, k;
                if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else { f = b ^ c ^ d; k = 0xCA62C1D6; }
                
                uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
                e = d; d = c; c = leftRotate(b, 30); b = a; a = temp;
            }
            
            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }
        
        uint8_t hash[20];
        for (int i = 0; i < 4; ++i) {
            hash[i] = (h0 >> (24 - i * 8)) & 0xFF;
            hash[i + 4] = (h1 >> (24 - i * 8)) & 0xFF;
            hash[i + 8] = (h2 >> (24 - i * 8)) & 0xFF;
            hash[i + 12] = (h3 >> (24 - i * 8)) & 0xFF;
            hash[i + 16] = (h4 >> (24 - i * 8)) & 0xFF;
        }
        
        static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        for (int i = 0; i < 20; i += 3) {
            uint32_t triple = (hash[i] << 16);
            if (i + 1 < 20) triple |= (hash[i + 1] << 8);
            if (i + 2 < 20) triple |= hash[i + 2];
            
            result += base64_chars[(triple >> 18) & 0x3F];
            result += base64_chars[(triple >> 12) & 0x3F];
            result += (i + 1 < 20) ? base64_chars[(triple >> 6) & 0x3F] : '=';
            result += (i + 2 < 20) ? base64_chars[triple & 0x3F] : '=';
        }
        
        return result;
    }
    
    std::vector<uint8_t> encodeFrame(const std::string& payload, uint8_t opcode) {
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | opcode);
        
        if (payload.size() < 126) {
            frame.push_back(static_cast<uint8_t>(payload.size()));
        } else if (payload.size() < 65536) {
            frame.push_back(126);
            frame.push_back((payload.size() >> 8) & 0xFF);
            frame.push_back(payload.size() & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((payload.size() >> (i * 8)) & 0xFF);
            }
        }
        
        frame.insert(frame.end(), payload.begin(), payload.end());
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
            payload[i] = data[offset + i] ^ mask[i % 4];
        }
        
        return {opcode, payload, offset + payload_len};
    }
    
    void removeClient(int client_id) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it != clients_.end()) {
            closesocket(it->second.socket);
            if (it->second.recv_thread.joinable() && 
                it->second.recv_thread.get_id() != std::this_thread::get_id()) {
                it->second.recv_thread.detach();
            }
            clients_.erase(it);
        }
    }
    
    std::atomic<bool> running_;
    SOCKET server_socket_;
    std::thread accept_thread_;
    std::map<int, Client> clients_;
    std::mutex clients_mutex_;
    std::atomic<int> next_client_id_;
    
    MessageCallback on_message_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
};

}
