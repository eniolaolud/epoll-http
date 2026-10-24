#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace http {

std::string url_decode(const std::string& in);
std::string html_escape(const std::string& in);
std::string mime_type(const std::string& path);
std::string trim(const std::string& s);
std::string to_lower(const std::string& s);

// Join root + relative path; returns nullopt on traversal / escape.
std::optional<std::string> safe_join(const std::string& root, const std::string& relative);

bool is_regular_file(const std::string& path);
bool path_exists(const std::string& path);
bool ensure_directory(const std::string& path);

std::string read_file(const std::string& path);
bool write_file(const std::string& path, const std::string& data);
bool remove_file(const std::string& path);

std::string basename_of(const std::string& path);
std::string dirname_of(const std::string& path);

std::vector<std::string> list_directory(const std::string& path);

}  // namespace http
