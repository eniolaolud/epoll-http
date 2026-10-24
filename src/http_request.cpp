#include "http_request.hpp"
#include "util.hpp"

#include <cstdlib>
#include <sstream>

namespace http {

std::string method_to_string(Method m) {
    switch (m) {
        case Method::GET: return "GET";
        case Method::POST: return "POST";
        case Method::DELETE: return "DELETE";
        case Method::HEAD: return "HEAD";
        case Method::PUT: return "PUT";
        case Method::OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

Method method_from_string(const std::string& s) {
    if (s == "GET") return Method::GET;
    if (s == "POST") return Method::POST;
    if (s == "DELETE") return Method::DELETE;
    if (s == "HEAD") return Method::HEAD;
    if (s == "PUT") return Method::PUT;
    if (s == "OPTIONS") return Method::OPTIONS;
    return Method::UNKNOWN;
}

std::optional<std::string> HttpRequest::header(const std::string& name) const {
    auto it = headers.find(to_lower(name));
    if (it == headers.end()) return std::nullopt;
    return it->second;
}

std::size_t HttpRequest::content_length() const {
    auto h = header("content-length");
    if (!h) return 0;
    try {
        return static_cast<std::size_t>(std::stoull(*h));
    } catch (...) {
        return 0;
    }
}

static void parse_cookies(HttpRequest& req) {
    auto h = req.header("cookie");
    if (!h) return;
    std::stringstream ss(*h);
    std::string part;
    while (std::getline(ss, part, ';')) {
        part = trim(part);
        auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string name = trim(part.substr(0, eq));
        std::string value = trim(part.substr(eq + 1));
        req.cookies[name] = value;
    }
}

static void parse_urlencoded(HttpRequest& req) {
    std::stringstream ss(req.body);
    std::string part;
    while (std::getline(ss, part, '&')) {
        if (part.empty()) continue;
        auto eq = part.find('=');
        std::string name = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
        std::string value = url_decode(eq == std::string::npos ? "" : part.substr(eq + 1));
        req.form_fields[name] = value;
    }
}

static std::string extract_boundary(const std::string& content_type) {
    auto pos = content_type.find("boundary=");
    if (pos == std::string::npos) return {};
    std::string b = content_type.substr(pos + 9);
    if (!b.empty() && b.front() == '"') {
        auto end = b.find('"', 1);
        if (end != std::string::npos) return b.substr(1, end - 1);
    }
    // trim trailing params
    auto semi = b.find(';');
    if (semi != std::string::npos) b = b.substr(0, semi);
    return trim(b);
}

static std::unordered_map<std::string, std::string> parse_disposition(const std::string& line) {
    std::unordered_map<std::string, std::string> attrs;
    auto sc = line.find(':');
    std::string rest = sc == std::string::npos ? line : line.substr(sc + 1);
    std::stringstream ss(rest);
    std::string part;
    while (std::getline(ss, part, ';')) {
        part = trim(part);
        auto eq = part.find('=');
        if (eq == std::string::npos) {
            attrs[to_lower(part)] = "";
            continue;
        }
        std::string k = to_lower(trim(part.substr(0, eq)));
        std::string v = trim(part.substr(eq + 1));
        if (!v.empty() && v.front() == '"' && v.back() == '"' && v.size() >= 2) {
            v = v.substr(1, v.size() - 2);
        }
        attrs[k] = v;
    }
    return attrs;
}

static void parse_multipart(HttpRequest& req, const std::string& boundary) {
    if (boundary.empty()) return;
    std::string delim = "--" + boundary;
    std::string end = delim + "--";

    std::size_t pos = 0;
    while (pos < req.body.size()) {
        auto start = req.body.find(delim, pos);
        if (start == std::string::npos) break;
        start += delim.size();
        if (start + 1 < req.body.size() && req.body[start] == '-' && req.body[start + 1] == '-') {
            break;  // closing boundary
        }
        if (start + 1 < req.body.size() && req.body[start] == '\r' && req.body[start + 1] == '\n') {
            start += 2;
        } else if (start < req.body.size() && req.body[start] == '\n') {
            start += 1;
        }

        auto next = req.body.find(delim, start);
        if (next == std::string::npos) break;
        std::size_t part_end = next;
        // strip trailing CRLF before next boundary
        if (part_end >= 2 && req.body[part_end - 2] == '\r' && req.body[part_end - 1] == '\n') {
            part_end -= 2;
        } else if (part_end >= 1 && req.body[part_end - 1] == '\n') {
            part_end -= 1;
        }

        std::string part = req.body.substr(start, part_end - start);
        auto hdr_end = part.find("\r\n\r\n");
        std::size_t body_off = 0;
        std::string headers_blob;
        if (hdr_end != std::string::npos) {
            headers_blob = part.substr(0, hdr_end);
            body_off = hdr_end + 4;
        } else {
            hdr_end = part.find("\n\n");
            if (hdr_end == std::string::npos) {
                pos = next;
                continue;
            }
            headers_blob = part.substr(0, hdr_end);
            body_off = hdr_end + 2;
        }
        std::string part_body = part.substr(body_off);

        std::string disposition;
        std::string ctype = "application/octet-stream";
        std::stringstream hs(headers_blob);
        std::string line;
        while (std::getline(hs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto lower = to_lower(line);
            if (lower.rfind("content-disposition:", 0) == 0) disposition = line;
            else if (lower.rfind("content-type:", 0) == 0) {
                auto c = line.find(':');
                ctype = trim(line.substr(c + 1));
            }
        }

        auto attrs = parse_disposition(disposition);
        std::string name = attrs.count("name") ? attrs["name"] : "";
        std::string filename = attrs.count("filename") ? attrs["filename"] : "";

        if (!filename.empty()) {
            UploadedFile f;
            f.field_name = name;
            f.filename = basename_of(filename);
            f.content_type = ctype;
            f.data = part_body;
            req.files.push_back(std::move(f));
        } else if (!name.empty()) {
            req.form_fields[name] = part_body;
        }

        pos = next;
    }
}

void finalize_request(HttpRequest& req) {
    parse_cookies(req);

    auto conn = req.header("connection");
    if (conn) {
        auto c = to_lower(*conn);
        if (c.find("close") != std::string::npos) req.keep_alive = false;
        else if (c.find("keep-alive") != std::string::npos) req.keep_alive = true;
    } else if (req.version == "HTTP/1.0") {
        req.keep_alive = false;
    }

    auto ct = req.header("content-type");
    if (!ct || req.body.empty()) return;
    auto ctl = to_lower(*ct);
    if (ctl.find("application/x-www-form-urlencoded") != std::string::npos) {
        parse_urlencoded(req);
    } else if (ctl.find("multipart/form-data") != std::string::npos) {
        parse_multipart(req, extract_boundary(*ct));
    }
}

int parse_request_headers(const std::string& buf, HttpRequest& out) {
    auto end = buf.find("\r\n\r\n");
    std::size_t sep_len = 4;
    if (end == std::string::npos) {
        end = buf.find("\n\n");
        if (end == std::string::npos) return 0;
        sep_len = 2;
    }

    std::string header_block = buf.substr(0, end);
    std::stringstream ss(header_block);
    std::string request_line;
    if (!std::getline(ss, request_line)) return -1;
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    std::stringstream rl(request_line);
    std::string method, target, version;
    if (!(rl >> method >> target >> version)) return -1;

    out = HttpRequest{};
    out.method_raw = method;
    out.method = method_from_string(method);
    out.target = target;
    out.version = version;

    auto qpos = target.find('?');
    if (qpos == std::string::npos) {
        out.path = url_decode(target);
        out.query.clear();
    } else {
        out.path = url_decode(target.substr(0, qpos));
        out.query = target.substr(qpos + 1);
    }
    if (out.path.empty()) out.path = "/";

    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = to_lower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        out.headers[name] = value;
    }

    return static_cast<int>(end + sep_len);
}

}  // namespace http
