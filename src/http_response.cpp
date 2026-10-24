#include "http_response.hpp"
#include "util.hpp"

#include <sstream>

namespace http {

static const char* default_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "OK";
    }
}

void HttpResponse::set_status(int code, const std::string& text) {
    status = code;
    status_text = text.empty() ? default_status_text(code) : text;
}

void HttpResponse::set_header(const std::string& name, const std::string& value) {
    headers[name] = value;
}

void HttpResponse::set_cookie(const std::string& name, const std::string& value,
                              const CookieOptions& opts) {
    std::ostringstream oss;
    oss << name << "=" << value;
    if (!opts.path.empty()) oss << "; Path=" << opts.path;
    if (opts.max_age >= 0) oss << "; Max-Age=" << opts.max_age;
    if (opts.http_only) oss << "; HttpOnly";
    if (opts.secure) oss << "; Secure";
    if (!opts.same_site.empty()) oss << "; SameSite=" << opts.same_site;
    set_cookies.push_back(oss.str());
}

void HttpResponse::set_body(const std::string& content, const std::string& content_type) {
    body = content;
    if (!content_type.empty()) set_header("Content-Type", content_type);
}

void HttpResponse::set_html(const std::string& html) {
    set_body(html, "text/html; charset=utf-8");
}

void HttpResponse::set_text(const std::string& text) {
    set_body(text, "text/plain; charset=utf-8");
}

void HttpResponse::set_file(const std::string& path, const std::string& content_type) {
    body = read_file(path);
    set_header("Content-Type", content_type.empty() ? mime_type(path) : content_type);
}

HttpResponse HttpResponse::not_found(const std::string& msg) {
    HttpResponse r;
    r.set_status(404, "Not Found");
    r.set_html("<!doctype html><html><body><h1>404 Not Found</h1><p>" +
               html_escape(msg) + "</p></body></html>");
    return r;
}

HttpResponse HttpResponse::bad_request(const std::string& msg) {
    HttpResponse r;
    r.set_status(400, "Bad Request");
    r.set_text(msg);
    return r;
}

HttpResponse HttpResponse::forbidden(const std::string& msg) {
    HttpResponse r;
    r.set_status(403, "Forbidden");
    r.set_text(msg);
    return r;
}

HttpResponse HttpResponse::method_not_allowed() {
    HttpResponse r;
    r.set_status(405, "Method Not Allowed");
    r.set_header("Allow", "GET, POST, DELETE, HEAD");
    r.set_text("Method Not Allowed");
    return r;
}

HttpResponse HttpResponse::internal_error(const std::string& msg) {
    HttpResponse r;
    r.set_status(500, "Internal Server Error");
    r.set_text(msg);
    return r;
}

HttpResponse HttpResponse::redirect(const std::string& location, int code) {
    HttpResponse r;
    r.set_status(code, default_status_text(code));
    r.set_header("Location", location);
    r.set_html("<!doctype html><html><body><a href=\"" + html_escape(location) +
               "\">Redirect</a></body></html>");
    return r;
}

std::string HttpResponse::serialize(bool include_body) const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n";

    bool has_len = false;
    bool has_conn = false;
    bool has_server = false;
    for (const auto& kv : headers) {
        if (to_lower(kv.first) == "content-length") has_len = true;
        if (to_lower(kv.first) == "connection") has_conn = true;
        if (to_lower(kv.first) == "server") has_server = true;
        oss << kv.first << ": " << kv.second << "\r\n";
    }
    for (const auto& c : set_cookies) {
        oss << "Set-Cookie: " << c << "\r\n";
    }
    if (!has_server) oss << "Server: epoll-http/1.0\r\n";
    if (!has_conn) oss << "Connection: keep-alive\r\n";
    if (!has_len) {
        std::size_t len = include_body ? body.size() : 0;
        if (status == 204 || status == 304) len = 0;
        oss << "Content-Length: " << len << "\r\n";
    }
    oss << "\r\n";
    if (include_body && status != 204 && status != 304) {
        oss << body;
    }
    return oss.str();
}

}  // namespace http
