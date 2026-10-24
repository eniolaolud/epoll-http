#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <ctime>

namespace http {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int hi = hex_val(in[i + 1]);
            int lo = hex_val(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string html_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string mime_type(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "application/octet-stream";
    std::string ext = to_lower(path.substr(pos + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "txt") return "text/plain; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "wasm") return "application/wasm";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    return "application/octet-stream";
}

std::optional<std::string> safe_join(const std::string& root, const std::string& relative) {
    std::string rel = relative;
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());

    // Reject null bytes and obvious traversal tokens before resolve.
    if (rel.find('\0') != std::string::npos) return std::nullopt;

    std::string candidate = root;
    if (!candidate.empty() && candidate.back() != '/') candidate.push_back('/');
    candidate += rel;

    char resolved_root[PATH_MAX];
    char resolved_path[PATH_MAX];
    if (!realpath(root.c_str(), resolved_root)) return std::nullopt;

    // For non-existent files, resolve parent and append basename.
    if (!realpath(candidate.c_str(), resolved_path)) {
        std::string parent = dirname_of(candidate);
        std::string base = basename_of(candidate);
        if (base.empty() || base == "." || base == "..") return std::nullopt;
        char resolved_parent[PATH_MAX];
        if (!realpath(parent.c_str(), resolved_parent)) return std::nullopt;
        std::string full = std::string(resolved_parent) + "/" + base;
        std::string root_s(resolved_root);
        if (full.compare(0, root_s.size(), root_s) != 0) return std::nullopt;
        if (full.size() > root_s.size() && full[root_s.size()] != '/') return std::nullopt;
        return full;
    }

    std::string root_s(resolved_root);
    std::string path_s(resolved_path);
    if (path_s.compare(0, root_s.size(), root_s) != 0) return std::nullopt;
    if (path_s.size() > root_s.size() && path_s[root_s.size()] != '/') return std::nullopt;
    return path_s;
}

bool is_regular_file(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool path_exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool ensure_directory(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path.c_str(), 0755) == 0;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

bool remove_file(const std::string& path) {
    return ::unlink(path.c_str()) == 0;
}

std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string dirname_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::vector<std::string> list_directory(const std::string& path) {
    std::vector<std::string> names;
    DIR* d = opendir(path.c_str());
    if (!d) return names;
    while (auto* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

std::string http_date_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm {};
    gmtime_r(&t, &tm);
    char buf[64];
    // e.g. Sun, 06 Nov 1994 08:49:37 GMT
    if (std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm) == 0) {
        return {};
    }
    return buf;
}

}  // namespace http
