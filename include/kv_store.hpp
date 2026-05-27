#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace uring_kv {

/**
 * Point-in-time counters exposed by the STATS command.
 */
struct StoreStats {
    std::size_t connections{0};
    std::size_t keys{0};
    std::size_t operations{0};
    std::chrono::seconds uptime{0};
};

/**
 * Single-threaded in-memory string key-value store.
 *
 * Thread safety: KVStore intentionally has no internal locking. The server
 * architecture owns it from one UringLoop thread, so synchronization would
 * only add noise.
 */
class KVStore {
public:
    KVStore();

    /**
     * Increments the active connection counter.
     */
    void increment_connections();

    /**
     * Decrements the active connection counter when non-zero.
     */
    void decrement_connections();

    /**
     * Compatibility wrapper for older call sites.
     */
    void record_connection();

    /**
     * Stores or replaces a value for key.
     */
    void set(std::string key, std::string value);

    /**
     * Returns a copy of the value for key, or std::nullopt when absent.
     */
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;

    /**
     * Removes key from the store.
     *
     * Returns true when the key existed and was erased.
     */
    bool del(std::string_view key);

    /**
     * Records one processed protocol operation.
     */
    void record_operation();

    /**
     * Returns current store counters and uptime.
     */
    [[nodiscard]] StoreStats stats() const;

private:
    std::unordered_map<std::string, std::string> data_;
    std::chrono::steady_clock::time_point started_at_;
    std::size_t connections_{0};
    std::size_t operations_{0};
};

} // namespace uring_kv
