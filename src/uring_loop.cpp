#include "uring_loop.hpp"

#include "protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace uring_kv {

namespace {

enum class Op : std::uint32_t {
    Accept = 0,
    Recv = 1,
    Send = 2,
};

std::atomic_bool* g_stop_flag{nullptr};

std::uint64_t encode(Op op, int fd)
{
    const auto op_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(op));
    const auto fd_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(fd));
    return (op_bits << 32U) | fd_bits;
}

Op decode_op(std::uint64_t user_data)
{
    return static_cast<Op>(static_cast<std::uint32_t>(user_data >> 32U));
}

int decode_fd(std::uint64_t user_data)
{
    const auto fd_bits = static_cast<std::uint32_t>(user_data & 0xffffffffULL);
    return static_cast<int>(static_cast<std::int32_t>(fd_bits));
}

void signal_handler(int)
{
    if (g_stop_flag != nullptr) {
        g_stop_flag->store(true);
    }
}

void throw_errno(char const* message)
{
    throw std::system_error(errno, std::generic_category(), message);
}

void throw_uring(int result, char const* message)
{
    throw std::system_error(-result, std::generic_category(), message);
}

class SignalGuard {
public:
    explicit SignalGuard(std::atomic_bool& stop_flag)
    {
        g_stop_flag = &stop_flag;

        struct sigaction action {};
        action.sa_handler = signal_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;

        if (sigaction(SIGINT, &action, &old_int_) != 0) {
            throw_errno("sigaction(SIGINT)");
        }
        int_installed_ = true;

        if (sigaction(SIGTERM, &action, &old_term_) != 0) {
            throw_errno("sigaction(SIGTERM)");
        }
        term_installed_ = true;
    }

    ~SignalGuard()
    {
        if (int_installed_) {
            (void)sigaction(SIGINT, &old_int_, nullptr);
        }
        if (term_installed_) {
            (void)sigaction(SIGTERM, &old_term_, nullptr);
        }
        g_stop_flag = nullptr;
    }

    SignalGuard(SignalGuard const&) = delete;
    SignalGuard& operator=(SignalGuard const&) = delete;

private:
    struct sigaction old_int_ {};
    struct sigaction old_term_ {};
    bool int_installed_{false};
    bool term_installed_{false};
};

} // namespace

UringLoop::UringLoop(ServerConfig config)
    : config_(std::move(config))
{
    setup_ring();
    setup_listen_socket();
}

UringLoop::~UringLoop()
{
    for (auto const& [fd, _] : read_buffers_) {
        (void)::close(fd);
    }
    read_buffers_.clear();
    recv_buffers_.clear();
    write_buffers_.clear();
    write_offsets_.clear();

    if (listen_fd_ >= 0) {
        (void)::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (ring_initialized_) {
        io_uring_queue_exit(&ring_);
        ring_initialized_ = false;
    }
}

void UringLoop::start()
{
    if (running_) {
        return;
    }

    stop_.store(false);
    submit_accept();
    running_ = true;
}

void UringLoop::run()
{
    SignalGuard signal_guard{stop_};

    if (!running_) {
        start();
    }

    while (!stop_.load() || !read_buffers_.empty()) {
        io_uring_cqe* cqe{nullptr};
        const int wait_result = io_uring_wait_cqe(&ring_, &cqe);
        if (wait_result == -EINTR) {
            continue;
        }
        if (wait_result < 0) {
            throw_uring(wait_result, "io_uring_wait_cqe");
        }

        handle_completion(*cqe);
        io_uring_cqe_seen(&ring_, cqe);
    }

    running_ = false;
}

void UringLoop::stop()
{
    stop_.store(true);
}

KVStore const& UringLoop::store() const
{
    return store_;
}

void UringLoop::setup_listen_socket()
{
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        throw_errno("socket");
    }

    int reuse{1};
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        throw_errno("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        throw std::invalid_argument("host must be a valid IPv4 address");
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        throw_errno("bind");
    }

    if (::listen(listen_fd_, SOMAXCONN) != 0) {
        throw_errno("listen");
    }
}

void UringLoop::setup_ring()
{
    const unsigned entries = config_.ring_entries == 0 ? 256U : config_.ring_entries;
    const int result = io_uring_queue_init(entries, &ring_, 0);
    if (result < 0) {
        throw_uring(result, "io_uring_queue_init");
    }
    ring_initialized_ = true;
}

void UringLoop::submit_accept()
{
    if (stop_.load()) {
        return;
    }

    io_uring_sqe* sqe = get_sqe();
    io_uring_prep_accept(sqe, listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    sqe->user_data = encode(Op::Accept, listen_fd_);
    submit_or_throw();
}

void UringLoop::submit_recv(int fd)
{
    auto& buffer = recv_buffers_[fd];
    buffer.assign(config_.receive_buffer_size, '\0');

    io_uring_sqe* sqe = get_sqe();
    io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0);
    sqe->user_data = encode(Op::Recv, fd);
    submit_or_throw();
}

void UringLoop::submit_send(int fd, std::string response)
{
    if (!response.empty()) {
        write_buffers_[fd] = std::move(response);
        write_offsets_[fd] = 0;
    }

    auto buffer_it = write_buffers_.find(fd);
    if (buffer_it == write_buffers_.end()) {
        submit_recv(fd);
        return;
    }

    const auto offset_it = write_offsets_.find(fd);
    const std::size_t offset = offset_it == write_offsets_.end() ? 0 : offset_it->second;
    if (offset >= buffer_it->second.size()) {
        write_buffers_.erase(buffer_it);
        write_offsets_.erase(fd);
        submit_recv(fd);
        return;
    }

    io_uring_sqe* sqe = get_sqe();
    const auto* data = buffer_it->second.data() + offset;
    const auto size = buffer_it->second.size() - offset;
    io_uring_prep_send(sqe, fd, data, size, 0);
    sqe->user_data = encode(Op::Send, fd);
    submit_or_throw();
}

void UringLoop::handle_completion(io_uring_cqe const& cqe)
{
    const auto op = decode_op(cqe.user_data);
    const int fd = decode_fd(cqe.user_data);

    switch (op) {
    case Op::Accept:
        handle_accept(cqe.res);
        return;
    case Op::Recv:
        handle_recv(fd, cqe.res);
        return;
    case Op::Send:
        handle_send(fd, cqe.res);
        return;
    }

    throw std::runtime_error("unknown io_uring operation");
}

void UringLoop::handle_accept(int result)
{
    if (result < 0) {
        if (!stop_.load()) {
            submit_accept();
        }
        return;
    }

    const int fd = result;
    if (stop_.load()) {
        (void)::close(fd);
        return;
    }

    store_.increment_connections();
    read_buffers_.try_emplace(fd);
    submit_recv(fd);
    submit_accept();
}

void UringLoop::handle_recv(int fd, int result)
{
    if (result <= 0) {
        close_connection(fd);
        return;
    }

    auto recv_it = recv_buffers_.find(fd);
    if (recv_it == recv_buffers_.end()) {
        close_connection(fd);
        return;
    }

    auto& read_buffer = read_buffers_[fd];
    read_buffer.append(recv_it->second.data(), static_cast<std::size_t>(result));

    const auto parsed = protocol::parse_buffer(
        std::span<const char>{read_buffer.data(), read_buffer.size()},
        {}
    );
    read_buffer = parsed.remainder;

    std::string response;
    for (const auto error : parsed.errors) {
        response.append(protocol::format_error(error));
    }
    for (auto const& command : parsed.commands) {
        response.append(protocol::execute(command, store_));
    }

    if (response.empty()) {
        submit_recv(fd);
        return;
    }

    submit_send(fd, std::move(response));
}

void UringLoop::handle_send(int fd, int result)
{
    if (result <= 0) {
        close_connection(fd);
        return;
    }

    auto buffer_it = write_buffers_.find(fd);
    if (buffer_it == write_buffers_.end()) {
        submit_recv(fd);
        return;
    }

    auto& offset = write_offsets_[fd];
    offset += static_cast<std::size_t>(result);

    if (offset < buffer_it->second.size()) {
        submit_send(fd, {});
        return;
    }

    write_buffers_.erase(buffer_it);
    write_offsets_.erase(fd);
    submit_recv(fd);
}

void UringLoop::close_connection(int fd)
{
    const bool was_active = read_buffers_.erase(fd) > 0;
    recv_buffers_.erase(fd);
    write_buffers_.erase(fd);
    write_offsets_.erase(fd);

    if (was_active) {
        store_.decrement_connections();
    }

    (void)::close(fd);
}

io_uring_sqe* UringLoop::get_sqe()
{
    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
        throw std::runtime_error("io_uring submission queue is full");
    }
    return sqe;
}

void UringLoop::submit_or_throw()
{
    const int result = io_uring_submit(&ring_);
    if (result < 0) {
        throw_uring(result, "io_uring_submit");
    }
}

} // namespace uring_kv
