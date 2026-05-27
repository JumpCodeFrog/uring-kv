#include "kv_store.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

void test_set_then_get_returns_value()
{
    uring_kv::KVStore store;
    store.set("name", "thomas");

    const auto value = store.get("name");
    assert(value.has_value());
    assert(*value == "thomas");
}

void test_get_missing_returns_nullopt()
{
    uring_kv::KVStore store;

    const auto value = store.get("missing");
    assert(!value.has_value());
}

void test_delete_semantics()
{
    uring_kv::KVStore store;
    store.set("name", "thomas");

    assert(store.del("name"));
    assert(!store.get("name").has_value());
    assert(!store.del("name"));
}

void test_stats_key_count_after_set_and_delete()
{
    uring_kv::KVStore store;
    assert(store.stats().keys == 0);

    store.set("one", "1");
    store.set("two", "2");
    store.set("two", "updated");
    assert(store.stats().keys == 2);

    assert(store.del("one"));
    assert(store.stats().keys == 1);

    assert(!store.del("missing"));
    assert(store.stats().keys == 1);
}

void test_connection_and_operation_counters()
{
    uring_kv::KVStore store;

    store.increment_connections();
    store.increment_connections();
    assert(store.stats().connections == 2);

    store.decrement_connections();
    assert(store.stats().connections == 1);

    store.decrement_connections();
    store.decrement_connections();
    assert(store.stats().connections == 0);

    store.record_operation();
    store.record_operation();
    assert(store.stats().operations == 2);
}

void test_uptime_is_non_negative_seconds()
{
    uring_kv::KVStore store;
    assert(store.stats().uptime >= std::chrono::seconds{0});
}

} // namespace

int main()
{
    test_set_then_get_returns_value();
    test_get_missing_returns_nullopt();
    test_delete_semantics();
    test_stats_key_count_after_set_and_delete();
    test_connection_and_operation_counters();
    test_uptime_is_non_negative_seconds();

    std::cout << "test_kv: all tests passed\n";
    return 0;
}
