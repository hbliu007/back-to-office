#include "daemon/daemon_client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <sstream>
#include <stdexcept>

namespace bto::daemon {

auto DaemonClient::request(const Json& payload) const -> Json {
    boost::asio::io_context ioc;
    boost::asio::local::stream_protocol::socket socket(ioc);
    socket.connect(socket_path_);

    const auto encoded = payload.dump() + "\n";
    boost::asio::write(socket, boost::asio::buffer(encoded));

    boost::asio::streambuf buffer;
    boost::asio::read_until(socket, buffer, '\n');

    std::istream input(&buffer);
    std::string line;
    std::getline(input, line);
    if (line.empty()) {
        throw std::runtime_error("empty daemon response");
    }
    return Json::parse(line);
}

auto DaemonClient::is_available() const -> bool {
    try {
        auto response = request(Json{
            {"action", "status"},
        });
        return response.value("ok", false);
    } catch (...) {
        return false;
    }
}

}  // namespace bto::daemon
