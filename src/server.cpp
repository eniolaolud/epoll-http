#include "server.hpp"
#include "util.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdio>

namespace http {

namespace {
constexpr int kMaxEvents = 128;
constexpr int kListenBacklog = 128;
}

Server::Server(std::string host, uint16_t port,
               std::string doc_root, std::string cgi_root)
    : host_(std::move(host)),
      port_(port),
      doc_root_(std::move(doc_root)),
      cgi_root_(std::move(cgi_root)) {}

Server::~Server() {
    stop();
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool Server::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        std::perror("socket");
        return false;
    }

    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (!set_nonblocking(listen_fd_)) {
        std::perror("fcntl");
        return false;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (host_ == "0.0.0.0" || host_.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid bind address: " << host_ << "\n";
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return false;
    }
    if (::listen(listen_fd_, kListenBacklog) < 0) {
        std::perror("listen");
        return false;
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::perror("epoll_create1");
        return false;
    }

    epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        std::perror("epoll_ctl");
        return false;
    }

    ensure_directory(doc_root_ + "/uploads");
    return true;
}

void Server::stop() {
    running_ = false;
}

void Server::accept_new() {
    while (true) {
        sockaddr_in client {};
        socklen_t len = sizeof(client);
        int cfd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&client),
                            &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            std::perror("accept");
            break;
        }

        epoll_event ev {};
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = cfd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev) < 0) {
            std::perror("epoll_ctl add client");
            ::close(cfd);
            continue;
        }
        conns_[cfd] = std::make_unique<Connection>(cfd);
    }
}

void Server::remove_connection(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    it->second->close();
    conns_.erase(it);
}

void Server::handle_event(int fd, uint32_t events) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Connection& conn = *it->second;

    if (events & (EPOLLERR | EPOLLHUP)) {
        remove_connection(fd);
        return;
    }

    if (events & EPOLLIN) {
        bool ready = conn.on_readable();
        if (conn.state() == ConnState::Closed) {
            remove_connection(fd);
            return;
        }
        if (ready) {
            // Request complete — handle and write
            const HttpRequest& req = conn.request();
            bool head = req.method == Method::HEAD;
            HttpResponse resp = handle_request(req);
            conn.queue_response(resp, !head);
            bool more = conn.on_writable();
            if (conn.state() == ConnState::Closed) {
                remove_connection(fd);
                return;
            }
            // Pipelined follow-ups
            while (more && conn.state() != ConnState::Closed) {
                if (conn.state() == ConnState::Writing) {
                    more = conn.on_writable();
                    continue;
                }
                // Next request already parsed
                head = conn.request().method == Method::HEAD;
                resp = handle_request(conn.request());
                conn.queue_response(resp, !head);
                more = conn.on_writable();
            }
            if (conn.state() == ConnState::Closed) {
                remove_connection(fd);
                return;
            }
        }
    }

    if (events & EPOLLOUT) {
        if (conn.want_write()) {
            bool more = conn.on_writable();
            if (conn.state() == ConnState::Closed) {
                remove_connection(fd);
                return;
            }
            while (more && conn.state() != ConnState::Closed &&
                   conn.state() != ConnState::Writing) {
                bool head = conn.request().method == Method::HEAD;
                HttpResponse resp = handle_request(conn.request());
                conn.queue_response(resp, !head);
                more = conn.on_writable();
            }
            if (conn.state() == ConnState::Closed) {
                remove_connection(fd);
                return;
            }
        }
    }
}

void Server::run() {
    running_ = true;
    std::vector<epoll_event> events(kMaxEvents);
    std::cout << "Listening on http://" << (host_.empty() ? "0.0.0.0" : host_)
              << ":" << port_ << "  doc_root=" << doc_root_
              << "  cgi_root=" << cgi_root_ << std::endl;

    while (running_) {
        int n = epoll_wait(epoll_fd_, events.data(), kMaxEvents, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                accept_new();
            } else {
                handle_event(fd, events[i].events);
            }
        }
    }
}

HttpResponse Server::list_uploads() {
    auto uploads = safe_join(doc_root_, "uploads");
    if (!uploads) return HttpResponse::internal_error("uploads path invalid");
    auto names = list_directory(*uploads);
    std::ostringstream oss;
    oss << "{\"files\":[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) oss << ",";
        oss << "\"" << html_escape(names[i]) << "\"";
    }
    oss << "]}";
    HttpResponse r;
    r.set_body(oss.str(), "application/json; charset=utf-8");
    return r;
}

HttpResponse Server::handle_api_cookies(const HttpRequest& req) {
    if (req.method == Method::GET) {
        std::ostringstream oss;
        oss << "<!doctype html><html lang=en><head><meta charset=utf-8>"
            << "<meta name=viewport content=\"width=device-width, initial-scale=1\">"
            << "<title>Cookies</title><link rel=stylesheet href=/style.css></head><body>"
            << "<header class=hero><p class=brand>epoll-http</p>"
            << "<h1>Cookies</h1>"
            << "<p class=lede>Values from the browser Cookie header, parsed into a map.</p>"
            << "</header><main><section><ul>";
        if (req.cookies.empty()) {
            oss << "<li><em>No cookies sent</em></li>";
        } else {
            for (const auto& kv : req.cookies) {
                oss << "<li><strong>" << html_escape(kv.first) << "</strong> = "
                    << html_escape(kv.second) << "</li>";
            }
        }
        oss << "</ul><p><a href=/>Home</a></p></section></main></body></html>";
        HttpResponse r;
        r.set_html(oss.str());
        return r;
    }
    if (req.method == Method::POST) {
        std::string name = req.form_fields.count("name") ? req.form_fields.at("name") : "demo";
        std::string value = req.form_fields.count("value") ? req.form_fields.at("value") : "1";
        if (name.empty()) name = "demo";
        // sanitize cookie name/value lightly
        for (char c : name) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
                return HttpResponse::bad_request("Invalid cookie name");
            }
        }
        HttpResponse r = HttpResponse::redirect("/");
        CookieOptions opts;
        opts.path = "/";
        opts.max_age = 3600;
        opts.http_only = false;
        opts.same_site = "Lax";
        r.set_cookie(name, value, opts);
        return r;
    }
    return HttpResponse::method_not_allowed();
}

HttpResponse Server::serve_static(const HttpRequest& req) {
    std::string path = req.path;
    if (path == "/") path = "/index.html";

    auto full = safe_join(doc_root_, path);
    if (!full) return HttpResponse::forbidden("Path not allowed");
    if (!is_regular_file(*full)) return HttpResponse::not_found(req.path);

    HttpResponse r;
    r.set_file(*full);
    r.set_header("Accept-Ranges", "none");
    // Treat /uploads/* as downloadable file transfers.
    if (path.rfind("/uploads/", 0) == 0) {
        std::string name = basename_of(path);
        if (!name.empty()) {
            r.set_header("Content-Disposition",
                         "attachment; filename=\"" + name + "\"");
        }
    }
    return r;
}

HttpResponse Server::handle_upload(const HttpRequest& req) {
    if (req.method != Method::POST) return HttpResponse::method_not_allowed();

    auto uploads_dir = safe_join(doc_root_, "uploads");
    if (!uploads_dir || !ensure_directory(*uploads_dir)) {
        return HttpResponse::internal_error("Cannot access uploads directory");
    }

    std::vector<std::string> saved;
    if (!req.files.empty()) {
        for (const auto& f : req.files) {
            std::string fname = basename_of(f.filename);
            if (fname.empty()) continue;
            // strip unsafe chars
            std::string clean;
            for (char c : fname) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') {
                    clean.push_back(c);
                }
            }
            if (clean.empty() || clean == "." || clean == "..") continue;
            auto dest = safe_join(*uploads_dir, clean);
            if (!dest) continue;
            if (!write_file(*dest, f.data)) {
                return HttpResponse::internal_error("Failed to write " + clean);
            }
            saved.push_back(clean);
        }
    } else {
        // Raw body upload with ?name= or form field filename
        std::string fname;
        if (req.form_fields.count("filename")) fname = req.form_fields.at("filename");
        // query name=
        {
            std::stringstream qs(req.query);
            std::string part;
            while (std::getline(qs, part, '&')) {
                auto eq = part.find('=');
                std::string k = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
                std::string v = url_decode(eq == std::string::npos ? "" : part.substr(eq + 1));
                if (k == "name" || k == "filename") fname = v;
            }
        }
        if (fname.empty()) fname = "upload.bin";
        fname = basename_of(fname);
        std::string clean;
        for (char c : fname) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') {
                clean.push_back(c);
            }
        }
        if (clean.empty()) return HttpResponse::bad_request("Missing filename");
        auto dest = safe_join(*uploads_dir, clean);
        if (!dest) return HttpResponse::forbidden("Bad path");
        if (!write_file(*dest, req.body)) {
            return HttpResponse::internal_error("Failed to write file");
        }
        saved.push_back(clean);
    }

    if (saved.empty()) return HttpResponse::bad_request("No file uploaded");

    auto accept = req.header("accept");
    bool want_html = !accept || accept->find("text/html") != std::string::npos;
    // form posts from browser typically want redirect
    auto ct = req.header("content-type");
    if (ct && ct->find("multipart/form-data") != std::string::npos) {
        return HttpResponse::redirect("/?uploaded=1");
    }

    if (want_html && req.header("x-requested-with")) {
        // fall through to JSON-ish text
    }

    std::ostringstream oss;
    oss << "Uploaded: ";
    for (std::size_t i = 0; i < saved.size(); ++i) {
        if (i) oss << ", ";
        oss << saved[i];
    }
    oss << "\n";
    HttpResponse r;
    r.set_status(201, "Created");
    r.set_text(oss.str());
    return r;
}

HttpResponse Server::handle_delete(const HttpRequest& req) {
    if (req.method != Method::DELETE && req.method != Method::POST) {
        return HttpResponse::method_not_allowed();
    }

    std::string name;
    // /uploads/<name> or /api/delete?name=
    if (req.path.rfind("/uploads/", 0) == 0) {
        name = req.path.substr(std::string("/uploads/").size());
    } else {
        std::stringstream qs(req.query);
        std::string part;
        while (std::getline(qs, part, '&')) {
            auto eq = part.find('=');
            std::string k = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
            std::string v = url_decode(eq == std::string::npos ? "" : part.substr(eq + 1));
            if (k == "name" || k == "file") name = v;
        }
        if (name.empty() && req.form_fields.count("name")) name = req.form_fields.at("name");
        if (name.empty() && req.form_fields.count("file")) name = req.form_fields.at("file");
    }

    name = basename_of(name);
    if (name.empty() || name == "." || name == "..") {
        return HttpResponse::bad_request("Missing file name");
    }

    auto uploads_dir = safe_join(doc_root_, "uploads");
    if (!uploads_dir) return HttpResponse::internal_error("uploads missing");
    auto full = safe_join(*uploads_dir, name);
    if (!full) return HttpResponse::forbidden("Bad path");
    if (!is_regular_file(*full)) return HttpResponse::not_found(name);
    if (!remove_file(*full)) return HttpResponse::internal_error("Delete failed");

    HttpResponse r;
    r.set_status(200, "OK");
    r.set_text("Deleted " + name + "\n");
    return r;
}

HttpResponse Server::handle_cgi(const HttpRequest& req) {
    // Map /cgi-bin/foo -> cgi_root_/foo
    std::string script_path = req.path;
    const std::string prefix = "/cgi-bin/";
    if (script_path.rfind(prefix, 0) != 0) {
        return HttpResponse::not_found(req.path);
    }
    std::string rel = script_path.substr(prefix.size());
    if (rel.empty() || rel.find('/') != std::string::npos) {
        return HttpResponse::forbidden("Invalid CGI path");
    }

    auto full = safe_join(cgi_root_, rel);
    if (!full || !is_regular_file(*full)) {
        return HttpResponse::not_found(req.path);
    }

    // Check executable bit
    if (access(full->c_str(), X_OK) != 0) {
        return HttpResponse::internal_error("CGI script not executable");
    }

    int inpipe[2];
    int outpipe[2];
    if (pipe(inpipe) < 0 || pipe(outpipe) < 0) {
        return HttpResponse::internal_error("pipe failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(inpipe[0]); ::close(inpipe[1]);
        ::close(outpipe[0]); ::close(outpipe[1]);
        return HttpResponse::internal_error("fork failed");
    }

    if (pid == 0) {
        // child
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        ::close(inpipe[0]); ::close(inpipe[1]);
        ::close(outpipe[0]); ::close(outpipe[1]);

        std::string method = method_to_string(req.method);
        std::string cl = std::to_string(req.body.size());
        std::string ct = req.header("content-type").value_or("");
        std::string cookie = req.header("cookie").value_or("");
        std::string host = req.header("host").value_or("");
        std::string ua = req.header("user-agent").value_or("");

        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
        setenv("SERVER_SOFTWARE", "epoll-http/1.0", 1);
        setenv("REQUEST_METHOD", method.c_str(), 1);
        setenv("SCRIPT_NAME", req.path.c_str(), 1);
        setenv("PATH_INFO", "", 1);
        setenv("QUERY_STRING", req.query.c_str(), 1);
        setenv("CONTENT_LENGTH", cl.c_str(), 1);
        setenv("CONTENT_TYPE", ct.c_str(), 1);
        setenv("HTTP_COOKIE", cookie.c_str(), 1);
        setenv("HTTP_HOST", host.c_str(), 1);
        setenv("HTTP_USER_AGENT", ua.c_str(), 1);
        setenv("REMOTE_ADDR", "127.0.0.1", 1);
        setenv("DOCUMENT_ROOT", doc_root_.c_str(), 1);

        execl(full->c_str(), full->c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    // parent
    ::close(inpipe[0]);
    ::close(outpipe[1]);

    // Write request body (may block briefly — documented limitation)
    std::size_t off = 0;
    while (off < req.body.size()) {
        ssize_t n = ::write(inpipe[1], req.body.data() + off, req.body.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<std::size_t>(n);
    }
    ::close(inpipe[1]);

    std::string cgi_out;
    char buf[4096];
    while (true) {
        ssize_t n = ::read(outpipe[0], buf, sizeof(buf));
        if (n > 0) {
            cgi_out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    ::close(outpipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        return HttpResponse::internal_error("CGI execution failed");
    }

    // Parse CGI headers (Status, Content-Type, etc.) until blank line
    HttpResponse resp;
    resp.set_status(200, "OK");
    auto hdr_end = cgi_out.find("\r\n\r\n");
    std::size_t body_off = 0;
    std::string hdrs;
    if (hdr_end != std::string::npos) {
        hdrs = cgi_out.substr(0, hdr_end);
        body_off = hdr_end + 4;
    } else {
        hdr_end = cgi_out.find("\n\n");
        if (hdr_end != std::string::npos) {
            hdrs = cgi_out.substr(0, hdr_end);
            body_off = hdr_end + 2;
        } else {
            // No headers — treat all as body
            resp.set_body(cgi_out, "text/plain; charset=utf-8");
            return resp;
        }
    }

    std::stringstream hs(hdrs);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        auto ln = to_lower(name);
        if (ln == "status") {
            // e.g. "200 OK" or "404 Not Found"
            std::stringstream vs(value);
            int code = 200;
            std::string text;
            vs >> code;
            std::getline(vs, text);
            text = trim(text);
            resp.set_status(code, text);
        } else if (ln == "set-cookie") {
            resp.set_cookies.push_back(value);
        } else {
            resp.set_header(name, value);
        }
    }
    resp.body = cgi_out.substr(body_off);
    if (!resp.headers.count("Content-Type") && !resp.headers.count("content-type")) {
        resp.set_header("Content-Type", "text/html; charset=utf-8");
    }
    return resp;
}

HttpResponse Server::handle_request(const HttpRequest& req) {
    std::cout << method_to_string(req.method) << " " << req.target << std::endl;

    if (req.method == Method::UNKNOWN) {
        return HttpResponse::bad_request("Unknown method");
    }
    if (req.method == Method::OPTIONS) {
        HttpResponse r;
        r.set_status(204, "No Content");
        r.set_header("Allow", "GET, POST, DELETE, HEAD, OPTIONS");
        return r;
    }

    // API routes
    if (req.path == "/api/uploads" && req.method == Method::GET) {
        return list_uploads();
    }
    if (req.path == "/api/upload" || req.path == "/upload") {
        return handle_upload(req);
    }
    if (req.path == "/api/delete" || req.path == "/delete") {
        return handle_delete(req);
    }
    if (req.path.rfind("/uploads/", 0) == 0 && req.method == Method::DELETE) {
        return handle_delete(req);
    }
    if (req.path == "/api/cookies" || req.path == "/cookies") {
        return handle_api_cookies(req);
    }

    // CGI
    if (req.path.rfind("/cgi-bin/", 0) == 0) {
        if (req.method != Method::GET && req.method != Method::POST &&
            req.method != Method::HEAD) {
            return HttpResponse::method_not_allowed();
        }
        return handle_cgi(req);
    }

    // Static GET/HEAD (and download of uploads via GET)
    if (req.method == Method::GET || req.method == Method::HEAD) {
        return serve_static(req);
    }

    // POST to other paths not handled
    if (req.method == Method::POST) {
        return HttpResponse::not_found(req.path);
    }
    if (req.method == Method::DELETE) {
        return handle_delete(req);
    }

    return HttpResponse::method_not_allowed();
}

}  // namespace http
