#pragma once

#include "kv_store.hpp"

#include <liburing.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace uring_kv {

/**
 * Runtime options for the single-threaded io_uring server.
 */
struct ServerConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{7777};
    unsigned ring_entries{256};
    std::size_t receive_buffer_size{4096};
    bool prefer_multishot_accept{false};
};

enum class OperationType {
    Accept,
    Recv,
    Send,
};

/**
 * Single-threaded event loop that owns one io_uring instance and all sockets.
 */
class UringLoop {
public:
    explicit UringLoop(ServerConfig config);
    ~UringLoop();

    UringLoop(UringLoop const&) = delete;
    UringLoop& operator=(UringLoop const&) = delete;
    UringLoop(UringLoop&&) = delete;
    UringLoop& operator=(UringLoop&&) = delete;

    /**
     * Initializes sockets and the io_uring instance.
     */
    void start();

    /**
     * Runs the blocking event loop until stop() is requested.
     */
    void run();

    /**
     * Requests graceful shutdown. In-flight completions are drained by run().
     */
    void stop();

    /**
     * Returns the store used by protocol command execution.
     */
    [[nodiscard]] KVStore const& store() const;

private:
    void setup_listen_socket();
    void setup_ring();
    void submit_accept();
    void submit_recv(int fd);
    void submit_send(int fd, std::string response);
    void handle_completion(io_uring_cqe const& cqe);
    void handle_accept(int result);
    void handle_recv(int fd, int result);
    void handle_send(int fd, int result);
    void close_connection(int fd);
    [[nodiscard]] io_uring_sqe* get_sqe();
    void submit_or_throw();

    ServerConfig config_;
    KVStore store_;
    io_uring ring_{};
    int listen_fd_{-1};
    bool ring_initialized_{false};
    bool running_{false};
    std::atomic_bool stop_{false};
    std::unordered_map<int, std::string> read_buffers_;
    std::unordered_map<int, std::string> recv_buffers_;
    std::unordered_map<int, std::string> write_buffers_;
    std::unordered_map<int, std::size_t> write_offsets_;
};

} // namespace uring_kv
