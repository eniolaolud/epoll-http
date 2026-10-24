#include "connection.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

namespace http {

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() {
    close();
}

bool Connection::want_read() const {
    return state_ == ConnState::ReadingHeaders || state_ == ConnState::ReadingBody;
}

bool Connection::want_write() const {
    return state_ == ConnState::Writing && write_offset_ < write_buf_.size();
}

void Connection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    state_ = ConnState::Closed;
}

bool Connection::body_complete() const {
    return read_buf_.size() >= header_end_ + expected_body_;
}

bool Connection::try_parse_headers() {
    int n = parse_request_headers(read_buf_, request_);
    if (n == 0) return false;
    if (n < 0) {
        state_ = ConnState::Closed;
        return false;
    }
    header_end_ = static_cast<std::size_t>(n);
    expected_body_ = request_.content_length();
    // Cap body size to 32 MiB
    if (expected_body_ > 32u * 1024u * 1024u) {
        state_ = ConnState::Closed;
        return false;
    }
    if (expected_body_ == 0) {
        request_.body.clear();
        finalize_request(request_);
        return true;
    }
    state_ = ConnState::ReadingBody;
    if (body_complete()) {
        request_.body = read_buf_.substr(header_end_, expected_body_);
        finalize_request(request_);
        return true;
    }
    return false;
}

bool Connection::on_readable() {
    char tmp[8192];
    while (true) {
        ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
        if (n > 0) {
            read_buf_.append(tmp, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            state_ = ConnState::Closed;
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        state_ = ConnState::Closed;
        return false;
    }

    if (state_ == ConnState::ReadingHeaders) {
        if (try_parse_headers()) return true;
        if (state_ == ConnState::Closed) return false;
        // Cap header size
        if (read_buf_.size() > 64 * 1024) {
            state_ = ConnState::Closed;
            return false;
        }
        return false;
    }

    if (state_ == ConnState::ReadingBody) {
        if (body_complete()) {
            request_.body = read_buf_.substr(header_end_, expected_body_);
            finalize_request(request_);
            return true;
        }
        if (read_buf_.size() > header_end_ + 32u * 1024u * 1024u) {
            state_ = ConnState::Closed;
            return false;
        }
    }
    return false;
}

void Connection::queue_response(const HttpResponse& resp, bool include_body) {
    keep_alive_ = request_.keep_alive;
    HttpResponse r = resp;
    if (!keep_alive_) r.set_header("Connection", "close");
    else r.set_header("Connection", "keep-alive");

    write_buf_ = r.serialize(include_body);
    write_offset_ = 0;
    state_ = ConnState::Writing;
}

bool Connection::on_writable() {
    while (write_offset_ < write_buf_.size()) {
        ssize_t n = ::send(fd_, write_buf_.data() + write_offset_,
                           write_buf_.size() - write_offset_, MSG_NOSIGNAL);
        if (n > 0) {
            write_offset_ += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;
        if (n < 0 && errno == EINTR) continue;
        state_ = ConnState::Closed;
        return true;
    }

    // Fully written
    if (!keep_alive_) {
        state_ = ConnState::Closed;
        return true;
    }

    // Prepare for next request on keep-alive connection
    std::size_t consumed = header_end_ + expected_body_;
    if (consumed < read_buf_.size()) {
        read_buf_.erase(0, consumed);
    } else {
        read_buf_.clear();
    }
    write_buf_.clear();
    write_offset_ = 0;
    header_end_ = 0;
    expected_body_ = 0;
    request_ = HttpRequest{};
    state_ = ConnState::ReadingHeaders;

    // Piped request already in buffer?
    if (!read_buf_.empty() && try_parse_headers()) {
        return true;  // signal caller that another request is ready
    }
    return false;
}

}  // namespace http
