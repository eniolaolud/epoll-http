#pragma once

#include "http_request.hpp"
#include "http_response.hpp"

#include <string>
#include <memory>
#include <cstdint>
#include <chrono>

namespace http {

enum class ConnState {
    ReadingHeaders,
    ReadingBody,
    Writing,
    Closed
};

class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return fd_; }
    ConnState state() const { return state_; }
    const HttpRequest& request() const { return request_; }
    bool want_read() const;
    bool want_write() const;
    bool keep_alive() const { return keep_alive_; }

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_activity() const { return last_activity_; }
    void touch();
    bool idle_timed_out(Clock::time_point now, int idle_seconds) const;

    // Returns true if a complete request is ready to handle.
    bool on_readable();
    // Queue a response for writing.
    void queue_response(const HttpResponse& resp, bool include_body = true);
    // Returns true when the write buffer is fully flushed (or connection closed).
    bool on_writable();
    void close();

private:
    int fd_ = -1;
    ConnState state_ = ConnState::ReadingHeaders;
    std::string read_buf_;
    std::string write_buf_;
    std::size_t write_offset_ = 0;
    HttpRequest request_;
    std::size_t header_end_ = 0;
    std::size_t expected_body_ = 0;
    bool keep_alive_ = true;
    Clock::time_point last_activity_;

    bool try_parse_headers();
    bool body_complete() const;
};

using ConnectionPtr = std::unique_ptr<Connection>;

}  // namespace http
