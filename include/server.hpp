#pragma once

#include "connection.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <csignal>

namespace http {

class Server {
public:
    Server(std::string host, uint16_t port,
           std::string doc_root, std::string cgi_root);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void run();
    void stop();

    HttpResponse handle_request(const HttpRequest& req);

private:
    std::string host_;
    uint16_t port_;
    std::string doc_root_;
    std::string cgi_root_;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::unordered_map<int, ConnectionPtr> conns_;
    std::atomic<bool> running_{false};

    bool set_nonblocking(int fd);
    void accept_new();
    void handle_event(int fd, uint32_t events);
    void remove_connection(int fd);

    HttpResponse serve_static(const HttpRequest& req);
    HttpResponse handle_upload(const HttpRequest& req);
    HttpResponse handle_delete(const HttpRequest& req);
    HttpResponse handle_cgi(const HttpRequest& req);
    HttpResponse handle_api_cookies(const HttpRequest& req);
    HttpResponse list_uploads();
};

}  // namespace http
