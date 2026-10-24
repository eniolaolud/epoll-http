#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace http {

enum class Method {
    GET,
    POST,
    DELETE,
    HEAD,
    PUT,
    OPTIONS,
    UNKNOWN
};

std::string method_to_string(Method m);
Method method_from_string(const std::string& s);

struct UploadedFile {
    std::string field_name;
    std::string filename;
    std::string content_type;
    std::string data;
};

struct HttpRequest {
    Method method = Method::UNKNOWN;
    std::string method_raw;
    std::string target;       // raw request-target
    std::string path;         // path without query
    std::string query;        // query string (no '?')
    std::string version;      // e.g. HTTP/1.1
    std::unordered_map<std::string, std::string> headers;  // lower-case keys
    std::unordered_map<std::string, std::string> cookies;
    std::unordered_map<std::string, std::string> form_fields;
    std::vector<UploadedFile> files;
    std::string body;
    bool keep_alive = true;

    std::optional<std::string> header(const std::string& name) const;
    std::size_t content_length() const;
};

// Parse headers + request line from buffer ending at "\r\n\r\n".
// Returns bytes consumed (including CRLFCRLF) or 0 if incomplete / -1 on error.
int parse_request_headers(const std::string& buf, HttpRequest& out);

// After headers are known and body is complete, parse cookies / form / multipart.
void finalize_request(HttpRequest& req);

}  // namespace http
