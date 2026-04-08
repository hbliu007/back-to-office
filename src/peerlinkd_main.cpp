#include "config/config.hpp"
#include "daemon/peerlink_service.hpp"

#include <boost/asio.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string socket_path = bto::config::default_daemon_socket_path();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "peerlinkd [--socket <path>]\n";
            return 0;
        }
    }

    try {
        boost::asio::io_context ioc;
        bto::daemon::PeerlinkDaemonServer server(ioc, socket_path);
        if (!server.start()) {
            std::cerr << "peerlinkd: failed to start on " << socket_path << "\n";
            return 1;
        }
        ioc.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "peerlinkd: " << ex.what() << "\n";
        return 1;
    }
}
