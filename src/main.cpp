#include "server.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::atomic<http::Server*> g_server{nullptr};

void on_signal(int) {
    if (auto* s = g_server.load()) s->stop();
}
}

static void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [--host HOST] [--port PORT] [--www DIR] [--cgi DIR]\n"
              << "  Defaults: host=0.0.0.0 port=38491 www=www cgi=cgi-bin\n";
}

int main(int argc, char** argv) {
    std::string host = "0.0.0.0";
    uint16_t port = 38491;
    std::string www = "www";
    std::string cgi = "cgi-bin";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--host") host = need("--host");
        else if (a == "--port") port = static_cast<uint16_t>(std::stoi(need("--port")));
        else if (a == "--www") www = need("--www");
        else if (a == "--cgi") cgi = need("--cgi");
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    http::Server server(host, port, www, cgi);
    g_server = &server;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    if (!server.start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    server.run();
    return 0;
}
