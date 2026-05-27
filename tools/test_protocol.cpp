#include "protocol.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using uring_kv::StoreStats;
using uring_kv::protocol::CommandType;
using uring_kv::protocol::ParseError;
using uring_kv::protocol::ParseResult;

ParseResult parse(std::string_view input, std::string_view previous_remainder = {})
{
    std::vector<char> buffer{input.begin(), input.end()};
    return uring_kv::protocol::parse_buffer(std::span<const char>{buffer.data(), buffer.size()},
                                            previous_remainder);
}

void test_ping()
{
    const auto result = parse("PING\r\n");
    assert(result.commands.size() == 1);
    assert(result.errors.empty());
    assert(result.remainder.empty());
    assert(result.commands.front().type == CommandType::Ping);
}

void test_partial_read()
{
    const auto first = parse("SET na");
    assert(first.commands.empty());
    assert(first.errors.empty());
    assert(first.remainder == "SET na");

    const auto second = parse("me thomas\r\n", first.remainder);
    assert(second.commands.size() == 1);
    assert(second.errors.empty());
    assert(second.remainder.empty());
    assert(second.commands.front().type == CommandType::Set);
    assert(second.commands.front().key == "name");
    assert(second.commands.front().value == "thomas");
}

void test_multiple_commands()
{
    const auto result = parse("SET title hello world\r\nGET title\r\nDEL title\r\nSTATS\r\n");
    assert(result.commands.size() == 4);
    assert(result.errors.empty());
    assert(result.commands[0].type == CommandType::Set);
    assert(result.commands[0].key == "title");
    assert(result.commands[0].value == "hello world");
    assert(result.commands[1].type == CommandType::Get);
    assert(result.commands[1].key == "title");
    assert(result.commands[2].type == CommandType::Del);
    assert(result.commands[2].key == "title");
    assert(result.commands[3].type == CommandType::Stats);
}

void test_malformed_lines()
{
    const auto result = parse("PING\nBAD x\r\nGET\r\nDEL key extra\r\n\r\nSTATS\r\n");
    assert(result.commands.size() == 1);
    assert(result.commands.front().type == CommandType::Stats);
    assert(result.errors.size() == 5);
    assert(result.errors[0] == ParseError::MissingCrlf);
    assert(result.errors[1] == ParseError::UnknownCommand);
    assert(result.errors[2] == ParseError::WrongArity);
    assert(result.errors[3] == ParseError::WrongArity);
    assert(result.errors[4] == ParseError::EmptyCommand);
}

void test_parse_line()
{
    const auto command = uring_kv::protocol::parse_line("GET name");
    assert(command.type == CommandType::Get);
    assert(command.key == "name");

    bool threw = false;
    try {
        (void)uring_kv::protocol::parse_line("PING now");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void test_formatters()
{
    using namespace uring_kv::protocol;

    assert(format_ok() == "OK\r\n");
    assert(format_pong() == "PONG\r\n");
    assert(format_value("thomas") == "thomas\r\n");
    assert(format_nil() == "(nil)\r\n");
    assert(format_error(ParseError::WrongArity) == "ERR wrong arity\r\n");

    const StoreStats stats{
        .connections = 3,
        .keys = 2,
        .operations = 9,
        .uptime = std::chrono::seconds{42},
    };
    assert(format_stats(stats) == "connections:3 keys:2 uptime:42s\r\n");
}

} // namespace

int main()
{
    test_ping();
    test_partial_read();
    test_multiple_commands();
    test_malformed_lines();
    test_parse_line();
    test_formatters();

    std::cout << "test_protocol: all tests passed\n";
    return 0;
}
