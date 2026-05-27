# uring-kv

## What & Why

`uring-kv` is an in-memory text-protocol key-value TCP server where accept, recv, and send are submitted through `io_uring`. Compared with an `epoll` loop that waits for readiness and then issues separate syscalls, this design keeps the hot I/O path completion-driven and makes the async state machine explicit.

## Architecture

```text
main()
  └── UringLoop
        ├── accept_loop()
        ├── read_conn()
        ├── process()
        └── write_conn()

KVStore
  └── std::unordered_map<string, string>
```

## Build & Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/uring_kv 127.0.0.1 7777
```

## Protocol

| Command | Response |
| --- | --- |
| `PING` | `PONG` |
| `SET key value` | `OK` |
| `GET key` | `value` or `(nil)` |
| `DEL key` | `OK` or `(nil)` |
| `STATS` | `connections:N keys:N uptime:Ns` |

Example session with `nc`:

```sh
printf 'PING\r\nSET name thomas\r\nGET name\r\nDEL name\r\nGET name\r\n' | nc 127.0.0.1 7777
```

```text
PONG
OK
thomas
OK
(nil)
```

## Benchmarks

Localhost results on a 12th Gen Intel(R) Core(TM) i3-1215U, Linux 6.17.0-29-generic, liburing 2.11:

| Test              | req/s     | p50 ms | p99 ms |
|-------------------|-----------|--------|--------|
| SET pipeline=1    | 47,263.08 | 0.018  | 0.049  |
| SET pipeline=32   | 181,553.43 | 0.154 | 0.322  |
| GET pipeline=32   | 173,882.64 | 0.162 | 0.335  |

See `bench/README.md` for environment notes.

## Design Decisions

The server is single-threaded by design: one event loop owns the `io_uring` ring, sockets, buffers, and `KVStore`, so the core data path has no locks and no cross-thread ownership transfers. Accept uses single-shot `io_uring_prep_accept()` and resubmits after each completion so the server remains compatible with Linux 5.10+ instead of requiring multishot accept from Linux 5.19+.

## What's Next

- Add CTest integration for `test_protocol` and `test_kv`.
- Add graceful draining for multiple in-flight sends per connection.
- Add optional multishot accept on Linux 5.19+.
- Add benchmark runs with a native client to reduce Python overhead.
