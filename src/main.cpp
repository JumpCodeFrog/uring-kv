#include "uring_loop.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint16_t parse_port(std::string_view text)
{
    unsigned long value{0};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);

    if (error != std::errc{} || ptr != end || value == 0 ||
        value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("port must be an integer in range 1..65535");
    }

    return static_cast<std::uint16_t>(value);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) {
            std::cerr << "usage: " << argv[0] << " <host> <port>\n";
            return 1;
        }

        const std::string host{argv[1]};
        const std::uint16_t port = parse_port(argv[2]);

        uring_kv::ServerConfig config{
            .host = host,
            .port = port,
        };
        uring_kv::UringLoop loop{std::move(config)};

        std::cout << "uring-kv listening on " << host << ':' << port << '\n';
        loop.run();
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "uring-kv: " << error.what() << '\n';
        return 1;
    }
}
