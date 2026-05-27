# PLAN

## Implementation Order

1. `src/kv_store.cpp` - implement counters, uptime, `SET`, `GET`, and `DEL`.
2. `src/protocol.cpp` - implement CRLF splitting, command parsing, response formatting, and store dispatch.
3. `src/main.cpp` - implement CLI parsing and signal wiring around `UringLoop`.
4. `src/uring_loop.cpp` setup path - implement listen socket creation, nonblocking mode, and `io_uring_queue_init`.
5. `src/uring_loop.cpp` accept path - submit accept SQEs, attach operation state, register connections, and resubmit accept.
6. `src/uring_loop.cpp` recv path - own per-connection receive buffers, parse complete commands, retain partial lines.
7. `src/uring_loop.cpp` send path - queue responses, handle partial sends, and return to recv only after pending output is flushed.
8. `src/uring_loop.cpp` shutdown path - handle disconnects, cancel in-flight work, drain late CQEs, and close fds.
9. Add parser/unit smoke tests or a small CTest target before replacing TODOs in production code.
10. Fill `README.md` and `bench/README.md` with run instructions and measured numbers.

## Research Findings And Deviations

- Upstream `axboe/liburing` on `master` is version 2.15 at commit `63bf649c4ee2935a2200ab823f89fd2a47284530` dated 2026-05-26.
- Current signatures in `src/include/liburing.h`:
  - `IOURINGINLINE struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring) LIBURING_NOEXCEPT`
  - `IOURINGINLINE void io_uring_prep_accept(struct io_uring_sqe *sqe, int fd, struct sockaddr *addr, socklen_t *addrlen, int flags) LIBURING_NOEXCEPT`
  - `IOURINGINLINE void io_uring_prep_recv(struct io_uring_sqe *sqe, int sockfd, void *buf, size_t len, int flags) LIBURING_NOEXCEPT`
  - `IOURINGINLINE void io_uring_prep_send(struct io_uring_sqe *sqe, int sockfd, const void *buf, size_t len, int flags) LIBURING_NOEXCEPT`
  - `int io_uring_submit(struct io_uring *ring) LIBURING_NOEXCEPT`
  - `IOURINGINLINE int io_uring_wait_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr) LIBURING_NOEXCEPT`
  - `IOURINGINLINE void io_uring_cqe_seen(struct io_uring *ring, struct io_uring_cqe *cqe) LIBURING_NOEXCEPT`
- `io_uring_prep_multishot_accept()` and `IORING_ACCEPT_MULTISHOT` are available in liburing >= 2.3, with runtime kernel support starting in Linux 5.19.
- The project specification targets Linux 5.10+, so the implementation must default to repeated single-shot `io_uring_prep_accept()` and treat multishot accept as an optional runtime/compile-time optimization.
- For multishot accept, completions must check `IORING_CQE_F_MORE`; when that flag is absent, the accept request is finished and must be resubmitted or replaced by fallback logic.
- C++20 examples from real repositories support using `std::span<std::byte>` / `std::span<const std::byte>` at I/O boundaries and small structured completion values for CQE dispatch.

## Biggest Technical Risk

The biggest risk is incorrect lifetime management for connection state and buffers while io_uring operations are still in flight. Closing a socket does not automatically cancel every pending operation, and late CQEs can arrive after the application has decided a connection is dead.

Handle it by making `Connection` ownership explicit, tracking each submitted operation through `user_data`, marking connections as closing before cancellation, keeping buffers alive until all expected CQEs are drained, and treating `cqe->res` negative values as operation results rather than `errno`.
