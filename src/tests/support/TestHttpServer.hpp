#pragma once

#ifdef _WIN32
// <cstdlib> ahead of the Winsock headers, for the same GCC-modules reason as
// core's Terminal.cppm: these suites also import cup modules.
#include <cstdlib>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

namespace cup::test {

namespace detail {

// The few spots where Winsock and BSD sockets differ: a SOCKET handle rather
// than an int fd, closesocket rather than close, and a WSAStartup the process
// must call once before any socket exists.
#ifdef _WIN32
using socket_t = SOCKET;
inline constexpr socket_t kNoSocket = INVALID_SOCKET;
inline constexpr int kShutBoth = SD_BOTH;
inline void close_socket(socket_t s) { ::closesocket(s); }
inline void ensure_sockets() {
    static const bool started = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    static_cast<void>(started);
}
#else
using socket_t = int;
inline constexpr socket_t kNoSocket = -1;
inline constexpr int kShutBoth = SHUT_RDWR;
inline void close_socket(socket_t s) { ::close(s); }
inline void ensure_sockets() {}
#endif

}

// A minimal single-purpose HTTP/1.1 server for tests: binds an ephemeral
// localhost port and, for every accepted connection, calls handler with the
// requested path and writes back the (status, body) it returns as the whole
// response. Stands in for Go's net/http/httptest.Server; it is not a
// general-purpose HTTP implementation (no keep-alive, no request bodies).
class TestHttpServer {
public:
    struct Response {
        int status = 200;
        std::string body;
    };
    using Handler = std::function<Response(const std::string& path)>;

    explicit TestHttpServer(Handler handler) : handler_(std::move(handler)) {
        detail::ensure_sockets();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // let the OS pick a free port
        ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifdef _WIN32
        int len = sizeof(addr);
#else
        socklen_t len = sizeof(addr);
#endif
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        ::listen(fd_, 16);
        thread_ = std::thread([this] { run(); });
    }

    TestHttpServer(const TestHttpServer&) = delete;
    TestHttpServer& operator=(const TestHttpServer&) = delete;

    ~TestHttpServer() {
        running_ = false;
        ::shutdown(fd_, detail::kShutBoth);
        detail::close_socket(fd_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    void run() {
        while (running_) {
            const detail::socket_t client = ::accept(fd_, nullptr, nullptr);
            if (client == detail::kNoSocket) {
                break;
            }
            handle_one(client);
            detail::close_socket(client);
        }
    }

    void handle_one(detail::socket_t client) const {
        char buf[4096];
        // A GET has no body, so the header block is the whole request; one read
        // is enough for anything these tests send.
        const auto n = ::recv(client, buf, sizeof(buf), 0);
        if (n <= 0) {
            return;
        }
        const std::string request(buf, static_cast<std::size_t>(n));

        std::string path = "/";
        if (const auto sp1 = request.find(' '); sp1 != std::string::npos) {
            if (const auto sp2 = request.find(' ', sp1 + 1); sp2 != std::string::npos) {
                path = request.substr(sp1 + 1, sp2 - sp1 - 1);
            }
        }

        const Response resp = handler_(path);
        std::ostringstream out;
        out << "HTTP/1.1 " << resp.status << " status\r\n"
            << "Content-Length: " << resp.body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << resp.body;
        const std::string data = out.str();
        ::send(client, data.data(), static_cast<int>(data.size()), 0);
    }

    Handler handler_;
    detail::socket_t fd_ = detail::kNoSocket;
    int port_ = 0;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

}
