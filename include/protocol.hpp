#pragma once

#include "kv_store.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uring_kv::protocol {

enum class CommandType {
    Ping,
    Set,
    Get,
    Del,
    Stats,
};

enum class ParseError {
    EmptyCommand,
    UnknownCommand,
    WrongArity,
    MissingCrlf,
};

/**
 * Parsed representation of one line-based protocol command.
 */
struct Command {
    CommandType type;
    std::string key;
    std::string value;
};

/**
 * Result of parsing a byte buffer that may contain zero or more complete lines.
 */
struct ParseResult {
    std::vector<Command> commands;
    std::string remainder;
    std::vector<ParseError> errors;
};

/**
 * Returns a stable human-readable label for parse errors.
 */
[[nodiscard]] std::string_view to_string(ParseError error);

/**
 * Parses CRLF-delimited commands from a raw socket receive buffer.
 *
 * The input span is a non-owning view. Parsed command fields are copied into
 * owning strings so callers may reuse the receive buffer after this call.
 */
[[nodiscard]] ParseResult parse_buffer(std::span<const char> bytes,
                                       std::string_view previous_remainder);

/**
 * Byte-buffer overload for callers that store socket data as std::byte.
 */
[[nodiscard]] ParseResult parse_buffer(std::span<const std::byte> bytes,
                                       std::string_view previous_remainder);

/**
 * Parses exactly one CRLF-free command line.
 */
[[nodiscard]] Command parse_line(std::string_view line);

/**
 * Formats a successful mutation response.
 */
[[nodiscard]] std::string format_ok();

/**
 * Formats the PING response.
 */
[[nodiscard]] std::string format_pong();

/**
 * Formats a present GET value.
 */
[[nodiscard]] std::string format_value(std::string_view value);

/**
 * Formats an absent GET/DEL result.
 */
[[nodiscard]] std::string format_nil();

/**
 * Applies a parsed command to the store and returns a CRLF-terminated response.
 */
[[nodiscard]] std::string execute(Command const& command, KVStore& store);

/**
 * Formats an error response for malformed input.
 */
[[nodiscard]] std::string format_error(ParseError error);

/**
 * Formats STATS output according to the project text protocol.
 */
[[nodiscard]] std::string format_stats(StoreStats const& stats);

} // namespace uring_kv::protocol
