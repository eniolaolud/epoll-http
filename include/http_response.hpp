#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace http {

struct CookieOptions {
    std::string path = "/";
    int max_age = -1;  // -1 = session cookie
    bool http_only = false;
    bool secure = false;
    std::string same_site;  // "Strict", "Lax", "None", or empty
};

class HttpResponse {
public:
    int status = 200;
    std::string status_text = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::string> set_cookies;
    std::string body;

    void set_status(int code, const std::string& text);
    void set_header(const std::string& name, const std::string& value);
    void set_cookie(const std::string& name, const std::string& value,
                    const CookieOptions& opts = {});
    void set_body(const std::string& content, const std::string& content_type);
    void set_html(const std::string& html);
    void set_text(const std::string& text);
    void set_file(const std::string& path, const std::string& content_type = "");

    static HttpResponse not_found(const std::string& msg = "Not Found");
    static HttpResponse bad_request(const std::string& msg = "Bad Request");
    static HttpResponse forbidden(const std::string& msg = "Forbidden");
    static HttpResponse method_not_allowed();
    static HttpResponse internal_error(const std::string& msg = "Internal Server Error");
    static HttpResponse redirect(const std::string& location, int code = 302);

    std::string serialize(bool include_body = true) const;
};

}  // namespace http
