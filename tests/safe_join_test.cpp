#include "util.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static std::string make_tmpdir() {
    char tmpl[] = "/tmp/epoll-http-sj-XXXXXX";
    char* p = mkdtemp(tmpl);
    assert(p);
    return p;
}

int main() {
    using http::safe_join;
    using http::ensure_directory;
    using http::write_file;

    std::string root = make_tmpdir();
    assert(ensure_directory(root + "/uploads"));
    assert(write_file(root + "/uploads/a.txt", "hi"));
    assert(write_file(root + "/index.html", "ok"));

    auto ok = safe_join(root, "index.html");
    assert(ok && ok->find(root) == 0);

    auto up = safe_join(root, "uploads/a.txt");
    assert(up && up->find("/uploads/a.txt") != std::string::npos);

    // Traversal must fail
    assert(!safe_join(root, "../etc/passwd"));
    assert(!safe_join(root, "uploads/../../etc/passwd"));
    assert(!safe_join(root, "/etc/passwd"));

    // New file under uploads (parent exists) should resolve under root
    auto neu = safe_join(root + "/uploads", "new.bin");
    assert(neu && neu->find(root) == 0);

    std::cout << "safe_join_test OK\n";
    return 0;
}
