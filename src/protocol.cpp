#include "protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace uring_kv::protocol {

namespace {

struct LineParseResult {
    std::optional<Command> command;
    ParseError error{ParseError::EmptyCommand};
};

LineParseResult parsed_command(Command command)
{
    return {.command = std::move(command), .error = ParseError::EmptyCommand};
}

LineParseResult parse_failure(ParseError error)
{
    return {.command = std::nullopt, .error = error};
}

bool is_space(char value)
{
    return value == ' ' || value == '\t';
}

std::string_view trim(std::string_view value)
{
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view ltrim(std::string_view value)
{
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    return value;
}

std::pair<std::string_view, std::string_view> take_token(std::string_view value)
{
    value = ltrim(value);
    const auto separator = std::ranges::find_if(value, is_space);
    const auto token_size = static_cast<std::size_t>(separator - value.begin());
    auto token = value.substr(0, token_size);
    auto rest = value.substr(token_size);
    return {token, ltrim(rest)};
}

std::string uppercase_ascii(std::string_view value)
{
    std::string upper;
    upper.reserve(value.size());
    for (const char ch : value) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return upper;
}

LineParseResult parse_line_checked(std::string_view line)
{
    line = trim(line);
    if (line.empty()) {
        return parse_failure(ParseError::EmptyCommand);
    }

    const auto [verb_view, after_verb] = take_token(line);
    const std::string verb = uppercase_ascii(verb_view);

    if (verb == "PING") {
        if (!trim(after_verb).empty()) {
            return parse_failure(ParseError::WrongArity);
        }
        return parsed_command(Command{CommandType::Ping, {}, {}});
    }

    if (verb == "STATS") {
        if (!trim(after_verb).empty()) {
            return parse_failure(ParseError::WrongArity);
        }
        return parsed_command(Command{CommandType::Stats, {}, {}});
    }

    if (verb == "GET" || verb == "DEL") {
        const auto [key, extra] = take_token(after_verb);
        if (key.empty() || !trim(extra).empty()) {
            return parse_failure(ParseError::WrongArity);
        }

        const auto type = verb == "GET" ? CommandType::Get : CommandType::Del;
        return parsed_command(Command{type, std::string{key}, {}});
    }

    if (verb == "SET") {
        const auto [key, value] = take_token(after_verb);
        const auto trimmed_value = trim(value);
        if (key.empty() || trimmed_value.empty()) {
            return parse_failure(ParseError::WrongArity);
        }
        return parsed_command(Command{CommandType::Set, std::string{key}, std::string{trimmed_value}});
    }

    return parse_failure(ParseError::UnknownCommand);
}

} // namespace

std::string_view to_string(ParseError error)
{
    switch (error) {
    case ParseError::EmptyCommand:
        return "empty command";
    case ParseError::UnknownCommand:
        return "unknown command";
    case ParseError::WrongArity:
        return "wrong arity";
    case ParseError::MissingCrlf:
        return "missing CRLF";
    }

    return "unknown parse error";
}

ParseResult parse_buffer(std::span<const char> bytes,
                         std::string_view previous_remainder)
{
    std::string input;
    input.reserve(previous_remainder.size() + bytes.size());
    input.append(previous_remainder);
    input.append(bytes.data(), bytes.size());

    ParseResult result;
    std::size_t position{0};

    while (position < input.size()) {
        const auto crlf = input.find("\r\n", position);
        const auto lf = input.find('\n', position);

        if (crlf == std::string::npos) {
            if (lf == std::string::npos) {
                result.remainder = input.substr(position);
                break;
            }

            result.errors.push_back(ParseError::MissingCrlf);
            position = lf + 1;
            continue;
        }

        if (lf != std::string::npos && lf < crlf) {
            result.errors.push_back(ParseError::MissingCrlf);
            position = lf + 1;
            continue;
        }

        const std::string_view line{input.data() + position, crlf - position};
        auto parsed = parse_line_checked(line);
        if (parsed.command.has_value()) {
            result.commands.push_back(std::move(*parsed.command));
        } else {
            result.errors.push_back(parsed.error);
        }

        position = crlf + 2;
    }

    return result;
}

ParseResult parse_buffer(std::span<const std::byte> bytes,
                         std::string_view previous_remainder)
{
    const auto* chars = reinterpret_cast<const char*>(bytes.data());
    return parse_buffer(std::span<const char>{chars, bytes.size()}, previous_remainder);
}

Command parse_line(std::string_view line)
{
    auto parsed = parse_line_checked(line);
    if (!parsed.command.has_value()) {
        throw std::invalid_argument(std::string{to_string(parsed.error)});
    }

    return std::move(*parsed.command);
}

std::string format_ok()
{
    return "OK\r\n";
}

std::string format_pong()
{
    return "PONG\r\n";
}

std::string format_value(std::string_view value)
{
    std::string response;
    response.reserve(value.size() + 2);
    response.append(value);
    response.append("\r\n");
    return response;
}

std::string format_nil()
{
    return "(nil)\r\n";
}

std::string execute(Command const& command, KVStore& store)
{
    store.record_operation();

    switch (command.type) {
    case CommandType::Ping:
        return format_pong();
    case CommandType::Set:
        store.set(command.key, command.value);
        return format_ok();
    case CommandType::Get:
        if (auto value = store.get(command.key); value.has_value()) {
            return format_value(*value);
        }
        return format_nil();
    case CommandType::Del:
        return store.del(command.key) ? format_ok() : format_nil();
    case CommandType::Stats:
        return format_stats(store.stats());
    }

    throw std::logic_error("unhandled command type");
}

std::string format_error(ParseError error)
{
    std::string response{"ERR "};
    response.append(to_string(error));
    response.append("\r\n");
    return response;
}

std::string format_stats(StoreStats const& stats)
{
    std::string response{"connections:"};
    response.append(std::to_string(stats.connections));
    response.append(" keys:");
    response.append(std::to_string(stats.keys));
    response.append(" uptime:");
    response.append(std::to_string(stats.uptime.count()));
    response.append("s\r\n");
    return response;
}

} // namespace uring_kv::protocol
