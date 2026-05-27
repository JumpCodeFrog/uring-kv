#include "kv_store.hpp"

#include <chrono>
#include <string>

namespace uring_kv {

KVStore::KVStore()
    : started_at_(std::chrono::steady_clock::now())
{
}

void KVStore::increment_connections()
{
    ++connections_;
}

void KVStore::decrement_connections()
{
    if (connections_ > 0) {
        --connections_;
    }
}

void KVStore::record_connection()
{
    increment_connections();
}

void KVStore::set(std::string key, std::string value)
{
    data_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<std::string> KVStore::get(std::string_view key) const
{
    const auto it = data_.find(std::string{key});
    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool KVStore::del(std::string_view key)
{
    return data_.erase(std::string{key}) > 0;
}

void KVStore::record_operation()
{
    ++operations_;
}

StoreStats KVStore::stats() const
{
    const auto elapsed = std::chrono::steady_clock::now() - started_at_;
    return StoreStats{
        .connections = connections_,
        .keys = data_.size(),
        .operations = operations_,
        .uptime = std::chrono::duration_cast<std::chrono::seconds>(elapsed),
    };
}

} // namespace uring_kv
