#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <fstream>
#include <cctype>
#include <cstdlib>
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
    struct HttpResponse {
        int status = 200;
        std::string content_type = "application/json; charset=utf-8";
        std::string body;
    };

    using RequestHandler = std::function<HttpResponse(const std::string& method, const std::string& path, const std::string& body)>;
    
    static AdminServer& instance() {
        static AdminServer inst;
        return inst;
    }
    
    bool start(
        const std::string& host = "127.0.0.1",
        int port = 8080,
        const std::string& auth_token = "",
        bool allow_public_readonly = false
    ) {
        if (running_) return true;
        
        bind_host_ = host.empty() ? "127.0.0.1" : host;
        port_ = port;
        auth_token_ = auth_token;
        allow_public_readonly_ = allow_public_readonly;

        if (auth_token_.empty() && !isLoopbackHost(bind_host_)) {
            LOG_ERROR("[Admin] Refusing non-local admin bind without admin_token");
            return false;
        }
        
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
        if (inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) <= 0) {
            struct hostent* host_entry = gethostbyname(bind_host_.c_str());
            if (host_entry == nullptr) {
                LOG_ERROR("[Admin] Failed to resolve host: " + bind_host_);
                closesocket(server_socket_);
                server_socket_ = INVALID_SOCKET;
                WSACleanup();
                return false;
            }
            addr.sin_addr = *reinterpret_cast<in_addr*>(host_entry->h_addr);
        }
        addr.sin_port = htons(port);
        
        if (bind(server_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("[Admin] Failed to bind " + bind_host_ + ":" + std::to_string(port));
            closesocket(server_socket_);
            server_socket_ = INVALID_SOCKET;
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
        
        LOG_INFO("[Admin] Server started on http://" + bind_host_ + ":" + std::to_string(port));
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
        std::map<std::string, std::string> headers;
        parseHttpRequest(request, method, path, body, headers);

        std::string handler_path = stripQueryString(path);

        if (handler_path == "/" || handler_path == "/index.html") {
            return buildHtmlResponse(getAdminPage());
        }

        if (isProtectedPath(handler_path, method) && !isAuthorized(path, method, handler_path, headers)) {
            if (handler_path == "/metrics") {
                return buildTextResponse({401, "text/plain; charset=utf-8", "Unauthorized\n"});
            }
            return buildJsonResponse("{\"error\":\"Unauthorized\"}", 401);
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = handlers_.find(handler_path);
        if (it != handlers_.end()) {
            return buildHandlerResponse(it->second(method, path, body));
        }
        
        for (const auto& [prefix, handler] : handlers_) {
            if (handler_path.find(prefix) == 0) {
                return buildHandlerResponse(handler(method, path, body));
            }
        }
        
        return buildJsonResponse("{\"error\":\"Not found\"}", 404);
    }
    
    void parseHttpRequest(
        const std::string& request,
        std::string& method,
        std::string& path,
        std::string& body,
        std::map<std::string, std::string>& headers
    ) {
        std::istringstream iss(request);
        std::string request_line;
        std::getline(iss, request_line);
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }

        std::istringstream request_line_stream(request_line);
        request_line_stream >> method >> path;

        std::string header_line;
        while (std::getline(iss, header_line)) {
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back();
            }
            if (header_line.empty()) {
                break;
            }

            size_t colon_pos = header_line.find(':');
            if (colon_pos == std::string::npos) {
                continue;
            }

            std::string key = toLower(trim(header_line.substr(0, colon_pos)));
            std::string value = trim(header_line.substr(colon_pos + 1));
            headers[key] = value;
        }
        
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
    }
    
    std::string buildJsonResponse(const std::string& json, int status = 200) {
        std::string status_text = getStatusText(status);
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << status_text << "\r\n";
        oss << "Content-Type: application/json; charset=utf-8\r\n";
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

    std::string buildTextResponse(const HttpResponse& response) {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << response.status << " " << getStatusText(response.status) << "\r\n";
        oss << "Content-Type: " << response.content_type << "\r\n";
        oss << "Content-Length: " << response.body.length() << "\r\n";
        oss << "\r\n";
        oss << response.body;
        return oss.str();
    }

    std::string buildHandlerResponse(const HttpResponse& response) {
        if (response.content_type.find("application/json") != std::string::npos) {
            return buildJsonResponse(response.body, response.status);
        }
        return buildTextResponse(response);
    }

    bool isProtectedPath(const std::string& path, const std::string& method) const {
        if (path == "/metrics" || path == "/api" || path.rfind("/api/", 0) == 0) {
            return true;
        }
        return !isReadOnlyMethod(method);
    }

    bool isAuthorized(
        const std::string& path,
        const std::string& method,
        const std::string& handler_path,
        const std::map<std::string, std::string>& headers
    ) const {
        if (allow_public_readonly_ && isReadOnlyMethod(method) && isPublicReadEndpoint(handler_path)) {
            return true;
        }

        if (auth_token_.empty()) {
            return true;
        }

        auto token_it = headers.find("x-admin-token");
        if (token_it != headers.end() && token_it->second == auth_token_) {
            return true;
        }

        auto auth_it = headers.find("authorization");
        if (auth_it != headers.end()) {
            const std::string bearer_prefix = "bearer ";
            std::string normalized_auth = toLower(auth_it->second);
            if (normalized_auth.rfind(bearer_prefix, 0) == 0) {
                std::string bearer_token = trim(auth_it->second.substr(bearer_prefix.length()));
                if (bearer_token == auth_token_) {
                    return true;
                }
            } else if (auth_it->second == auth_token_) {
                return true;
            }
        }

        return getQueryValue(path, "token") == auth_token_;
    }

    bool isPublicReadEndpoint(const std::string& path) const {
        return path == "/metrics" ||
            path == "/api/auth" ||
            path == "/api/stats" ||
            path == "/api/plugins" ||
            path == "/api/personalities" ||
            path == "/api/groups" ||
            path == "/api/metrics" ||
            path == "/api/permissions" ||
            path == "/api/traces" ||
            path == "/api/cache" ||
            path == "/api/sandbox";
    }

    static bool isReadOnlyMethod(const std::string& method) {
        return method == "GET" || method == "HEAD" || method == "OPTIONS";
    }

    static bool isLoopbackHost(const std::string& host) {
        return host == "127.0.0.1" || host == "localhost" || host == "::1";
    }

    static std::string stripQueryString(const std::string& path) {
        size_t query_pos = path.find('?');
        if (query_pos == std::string::npos) {
            return path;
        }
        return path.substr(0, query_pos);
    }

    static std::string getQueryValue(const std::string& path, const std::string& key) {
        size_t query_pos = path.find('?');
        if (query_pos == std::string::npos || query_pos + 1 >= path.length()) {
            return "";
        }

        std::stringstream query_stream(path.substr(query_pos + 1));
        std::string pair;
        while (std::getline(query_stream, pair, '&')) {
            size_t equal_pos = pair.find('=');
            if (equal_pos == std::string::npos) {
                continue;
            }

            std::string current_key = urlDecode(pair.substr(0, equal_pos));
            if (current_key != key) {
                continue;
            }

            return urlDecode(pair.substr(equal_pos + 1));
        }

        return "";
    }

    static std::string trim(const std::string& value) {
        size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    static std::string toLower(const std::string& value) {
        std::string result = value;
        for (char& ch : result) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return result;
    }

    static std::string urlDecode(const std::string& value) {
        std::string decoded;
        decoded.reserve(value.size());

        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '%' && index + 2 < value.size()) {
                std::string hex = value.substr(index + 1, 2);
                char decoded_char = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                decoded += decoded_char;
                index += 2;
                continue;
            }

            if (value[index] == '+') {
                decoded += ' ';
                continue;
            }

            decoded += value[index];
        }

        return decoded;
    }

    static std::string getStatusText(int status) {
        switch (status) {
            case 200: return "OK";
            case 401: return "Unauthorized";
            case 405: return "Method Not Allowed";
            case 404: return "Not Found";
            default: return "Error";
        }
    }
    
    std::string getAdminPage();
    
    std::atomic<bool> running_{false};
    std::string bind_host_ = "127.0.0.1";
    int port_ = 8080;
    std::string auth_token_;
    bool allow_public_readonly_ = false;
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
