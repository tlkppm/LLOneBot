#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include "../core/Logger.h"
#include "../core/JsonParser.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace LCHBOT {

class AdminServer {
public:
    using RequestHandler = std::function<std::string(const std::string& method, const std::string& path, const std::string& body)>;
    
    static AdminServer& instance() {
        static AdminServer inst;
        return inst;
    }
    
    bool start(int port = 8080) {
        if (running_) return true;
        
        port_ = port;
        
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOG_ERROR("[Admin] WSAStartup failed");
            return false;
        }
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET) {
            LOG_ERROR("[Admin] Failed to create socket");
            WSACleanup();
            return false;
        }
        
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(server_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("[Admin] Failed to bind port " + std::to_string(port));
            closesocket(server_socket_);
            WSACleanup();
            return false;
        }
        
        if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
            LOG_ERROR("[Admin] Failed to listen");
            closesocket(server_socket_);
            WSACleanup();
            return false;
        }
        
        running_ = true;
        server_thread_ = std::thread(&AdminServer::serverLoop, this);
        
        LOG_INFO("[Admin] Server started on http://127.0.0.1:" + std::to_string(port));
        return true;
#else
        return false;
#endif
    }
    
    void stop() {
        if (!running_) return;
        running_ = false;
        
#ifdef _WIN32
        closesocket(server_socket_);
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        WSACleanup();
#endif
        LOG_INFO("[Admin] Server stopped");
    }
    
    void registerHandler(const std::string& path, RequestHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[path] = handler;
    }
    
private:
    AdminServer() = default;
    ~AdminServer() { stop(); }
    
#ifdef _WIN32
    void serverLoop() {
        while (running_) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(server_socket_, &readSet);
            
            timeval timeout = {1, 0};
            int result = select(0, &readSet, nullptr, nullptr, &timeout);
            
            if (result > 0 && FD_ISSET(server_socket_, &readSet)) {
                sockaddr_in clientAddr;
                int clientAddrLen = sizeof(clientAddr);
                SOCKET clientSocket = accept(server_socket_, (sockaddr*)&clientAddr, &clientAddrLen);
                
                if (clientSocket != INVALID_SOCKET) {
                    std::thread(&AdminServer::handleClient, this, clientSocket).detach();
                }
            }
        }
    }
    
    void handleClient(SOCKET clientSocket) {
        char buffer[8192] = {0};
        int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (received > 0) {
            std::string request(buffer, received);
            std::string response = processRequest(request);
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        }
        
        closesocket(clientSocket);
    }
#endif
    
    std::string processRequest(const std::string& request) {
        std::string method, path, body;
        parseHttpRequest(request, method, path, body);
        
        if (path == "/" || path == "/index.html") {
            return buildHtmlResponse(getAdminPage());
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string handler_path = path;
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            handler_path = path.substr(0, query_pos);
        }
        
        auto it = handlers_.find(handler_path);
        if (it != handlers_.end()) {
            std::string result = it->second(method, path, body);
            return buildJsonResponse(result);
        }
        
        for (const auto& [prefix, handler] : handlers_) {
            if (handler_path.find(prefix) == 0) {
                std::string result = handler(method, path, body);
                return buildJsonResponse(result);
            }
        }
        
        return buildJsonResponse("{\"error\":\"Not found\"}", 404);
    }
    
    void parseHttpRequest(const std::string& request, std::string& method, std::string& path, std::string& body) {
        std::istringstream iss(request);
        iss >> method >> path;
        
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
    }
    
    std::string buildJsonResponse(const std::string& json, int status = 200) {
        std::string status_text = (status == 200) ? "OK" : "Not Found";
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << status_text << "\r\n";
        oss << "Content-Type: application/json; charset=utf-8\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
        oss << "Access-Control-Allow-Headers: Content-Type\r\n";
        oss << "Content-Length: " << json.length() << "\r\n";
        oss << "\r\n";
        oss << json;
        return oss.str();
    }
    
    std::string buildHtmlResponse(const std::string& html) {
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: text/html; charset=utf-8\r\n";
        oss << "Content-Length: " << html.length() << "\r\n";
        oss << "\r\n";
        oss << html;
        return oss.str();
    }
    
    std::string getAdminPage();
    
    std::atomic<bool> running_{false};
    int port_ = 8080;
    std::thread server_thread_;
    std::mutex mutex_;
    std::map<std::string, RequestHandler> handlers_;
    
#ifdef _WIN32
    SOCKET server_socket_ = INVALID_SOCKET;
#endif
};

inline std::string AdminServer::getAdminPage() {
    std::ifstream file("admin/index.html");
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    return "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>LCHBOT Admin</title>"
           "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px;}"
           ".card{background:#16213e;padding:20px;margin:10px;border-radius:8px;}"
           "h1{color:#667eea;}</style></head><body>"
           "<h1>LCHBOT Admin Panel</h1>"
           "<div class=\"card\"><h2>API Endpoints</h2>"
           "<p>GET /api/stats - Statistics</p>"
           "<p>GET /api/plugins - Plugin list</p>"
           "<p>GET /api/personalities - Personality list</p>"
           "<p>GET /api/groups - Active groups</p>"
           "<p>POST /api/plugins/{name}/enable - Enable plugin</p>"
           "<p>POST /api/plugins/{name}/disable - Disable plugin</p>"
           "<p>POST /api/reload - Reload system</p>"
           "</div></body></html>";
}

}
